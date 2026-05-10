#include "AnalysisEngine.h"
#include "Analysis/TextureAnalyzer.h"
#include "Analysis/PitchDetector.h"
#include "Analysis/SliceBoundary.h"
#include "Analysis/NoteSegmenter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

namespace switchblade::analysis
{
    namespace
    {
        // Returns the linear RMS amplitude of a sample range across all channels.
        // Used by the Top-4 guardrail to detect silent/empty slices.
        [[nodiscard]] float computeSliceRms (const juce::AudioBuffer<float>& buf,
                                             std::int64_t start, std::int64_t end)
        {
            const int numCh = buf.getNumChannels();
            const int total = buf.getNumSamples();
            const int s     = static_cast<int> (std::clamp (start, std::int64_t{0},
                                                             static_cast<std::int64_t> (total)));
            const int e     = static_cast<int> (std::clamp (end,   std::int64_t{0},
                                                             static_cast<std::int64_t> (total)));
            const int numS  = e - s;
            if (numCh <= 0 || numS <= 0)
                return 0.0f;

            double sumSq = 0.0;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* r = buf.getReadPointer (ch, s);
                for (int i = 0; i < numS; ++i)
                    sumSq += static_cast<double> (r[i]) * static_cast<double> (r[i]);
            }
            return static_cast<float> (std::sqrt (sumSq / static_cast<double> (numCh * numS)));
        }
    } // namespace

    //==========================================================================
    //  JSON serialisation
    //==========================================================================
    juce::String AnalysisResult::toJson (int indent) const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("jobId",           jobId);
        root->setProperty ("file",            juce::String (path.string()));
        root->setProperty ("sampleRate",      sampleRate);
        root->setProperty ("lengthInSamples", static_cast<juce::int64> (lengthInSamples));
        root->setProperty ("durationSeconds",
                           sampleRate > 0.0
                           ? static_cast<double> (lengthInSamples) / sampleRate
                           : 0.0);

        const auto modeStr = [&]() -> const char*
        {
            switch (requestedMode)
            {
                case AnalysisMode::Auto:       return "auto";
                case AnalysisMode::Percussive: return "percussive";
                case AnalysisMode::Melodic:    return "melodic";
                case AnalysisMode::Texture:    return "texture";
            }
            return "auto";
        }();
        root->setProperty ("mode", juce::String (modeStr));

        const auto classStr = [&]() -> const char*
        {
            switch (classification)
            {
                case SourceClass::Percussive: return "percussive";
                case SourceClass::Melodic:    return "melodic";
                case SourceClass::Texture:    return "texture";
                default:                      return "unknown";
            }
        }();
        root->setProperty ("class", juce::String (classStr));
        root->setProperty ("error", errorMessage);

        if (pitchHz.has_value() && pitchClarity.has_value())
        {
            root->setProperty ("pitchHz",     *pitchHz);
            root->setProperty ("pitchClarity", *pitchClarity);
            root->setProperty ("pitchNote",   juce::String (
                PitchDetector::noteNameFromHz (*pitchHz)));
        }

        juce::Array<juce::var> slices;
        slices.ensureStorageAllocated (static_cast<int> (transients.size()));
        for (const auto& t : transients)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("sample",     static_cast<juce::int64> (t.sampleIndex));
            o->setProperty ("rawSample",  static_cast<juce::int64> (t.rawSampleIndex));
            o->setProperty ("time",       t.timeSeconds);
            o->setProperty ("flux",       t.fluxValue);
            o->setProperty ("confidence", t.confidence);
            slices.add (juce::var (o));
        }
        root->setProperty ("transients", slices);

        // juce::JSON::toString overload accepting FormatOptions was introduced
        // in JUCE 7.0.9. If your JUCE version is older, replace this with:
        //   return juce::JSON::toString (juce::var (root), indent > 0);
        return juce::JSON::toString (
            juce::var (root),
            indent > 0
            ? juce::JSON::FormatOptions{}
                  .withSpacing (juce::JSON::Spacing::multiLine)
                  .withIndentLevel (indent)
            : juce::JSON::FormatOptions{});
    }

    //==========================================================================
    //  Job — one ThreadPoolJob per queued file
    //==========================================================================
    class AnalysisEngine::Job final : public juce::ThreadPoolJob
    {
    public:
        Job (AnalysisEngine& owner,
             int id,
             std::filesystem::path path,
             AnalysisMode mode)
            : juce::ThreadPoolJob ("switchblade.analyze"),
              owner_ (owner),
              id_ (id),
              path_ (std::move (path)),
              mode_ (mode) {}

        JobStatus runJob() override
        {
            // Fire onStarted on the message thread before any heavy work
            notifyStarted();

            AnalysisResult result;
            result.jobId         = id_;
            result.path          = path_;
            result.requestedMode = mode_;

            auto file = loadAudioFile (owner_.formatManager_, path_);
            if (shouldExit())
            {
                // Must always dispatch to decrement the in-flight counter.
                result.errorMessage = "Cancelled";
                dispatch (std::move (result));
                return jobHasFinished;
            }

            if (! file.has_value())
            {
                result.errorMessage = "Failed to load audio file: "
                                    + juce::String (path_.filename().string());
                dispatch (std::move (result));
                return jobHasFinished;
            }

            result.sampleRate      = file->sampleRate;
            result.lengthInSamples = file->originalLengthInSamples;

            switch (mode_)
            {
                case AnalysisMode::Texture:
                {
                    result.classification = SourceClass::Texture;
                    TextureAnalyzer::Params tp;
                    tp.sensitivity = owner_.detectorParams_.sensitivity;
                    TextureAnalyzer tex { tp };
                    result.transients = tex.analyze (*file);
                    break;
                }

                case AnalysisMode::Melodic:
                {
                    result.classification = SourceClass::Melodic;

                    // ── Note segmentation — pitch-based onset detection ───────
                    // Preferred over amplitude transients for melodic material:
                    // finds every individual note even when attacks are soft.
                    // Falls back to TransientDetector for staccato passages or
                    // if the pitch tracker can't find meaningful boundaries.
                    {
                        auto notes = segmentNotes (*file);
                        if (notes.size() >= 2)
                        {
                            result.transients = std::move (notes);
                        }
                        else
                        {
                            // Soft-attack material with no clear note transitions
                            // (e.g. sustained drones) — use transient detector as
                            // a fallback so at least one slice is produced.
                            TransientDetector det { owner_.detectorParams_ };
                            result.transients = det.detect (*file);
                        }
                    }

                    // ── Global pitch estimate (first stable frame) ────────────
                    // Represents the dominant pitch of the file; used for the
                    // note name badge in the UI and the filename keySuffix.
                    // TODO: per-note pitch for future key-per-slice naming.
                    {
                        std::vector<float> mono;
                        mixToMono (file->samples, mono);
                        if (! mono.empty())
                        {
                            PitchDetector pd;
                            constexpr std::size_t kFrame = 2048;
                            for (std::size_t off = 0;
                                 off + kFrame <= mono.size();
                                 off += kFrame / 2)
                            {
                                auto pr = pd.detect (
                                    std::span<const float> (mono.data() + off, kFrame),
                                    file->sampleRate);
                                if (pr.has_value() && pr->clarity > 0.5f)
                                {
                                    result.pitchHz = pr->f0Hz;
                                    result.pitchClarity = pr->clarity;
                                    break;
                                }
                            }
                        }
                    }
                    break;
                }

                case AnalysisMode::Auto:
                {
                    // --- Heuristic classifier -------------------------------------------
                    // 1. Detect onsets with the transient detector.
                    // 2. Estimate pitch clarity on the first stable frame.
                    // Decision tree:
                    //   clarity > 0.60 AND onsetRate < 5 /s  -> Melodic
                    //   clarity < 0.20 AND onsetRate < 1.5/s -> Texture (re-run analyzer)
                    //   otherwise                             -> Percussive

                    TransientDetector det { owner_.detectorParams_ };
                    auto onsets = det.detect (*file);

                    const double durationSec =
                        (file->sampleRate > 0.0 && file->originalLengthInSamples > 0)
                        ? static_cast<double> (file->originalLengthInSamples) / file->sampleRate
                        : 0.0;
                    const double onsetRate =
                        (durationSec > 0.0 && !onsets.empty())
                        ? static_cast<double> (onsets.size()) / durationSec
                        : 0.0;

                    float pitchClarity = 0.0f;
                    std::optional<float> autoPitchHz;
                    {
                        std::vector<float> mono;
                        mixToMono (file->samples, mono);
                        if (! mono.empty())
                        {
                            PitchDetector pd;
                            constexpr std::size_t kFrame = 2048;
                            for (std::size_t off = 0;
                                 off + kFrame <= mono.size();
                                 off += kFrame / 2)
                            {
                                auto pr = pd.detect (
                                    std::span<const float> (mono.data() + off, kFrame),
                                    file->sampleRate);
                                if (pr.has_value() && pr->clarity > pitchClarity)
                                {
                                    pitchClarity = pr->clarity;
                                    autoPitchHz  = pr->f0Hz;
                                    if (pitchClarity > 0.55f)
                                        break;  // good enough — stop scanning early
                                }
                            }
                        }
                    }

                    // Melodic threshold: tonal material (triangle, piano, singing,
                    // bell) typically has pitchClarity > 0.40 and sparse onsets.
                    // Kick/snare loops have high onsetRate (> 1.5/s) so the rate
                    // gate keeps them Percussive even if they have tonal content.
                    // Single-shot kicks are short (< 200ms) so their onsetRate
                    // from the single-onset fallback is >> 1.5/s.
                    if (pitchClarity > 0.40f && onsetRate < 1.5)
                    {
                        result.classification = SourceClass::Melodic;
                        result.pitchHz        = autoPitchHz;
                        result.pitchClarity   = pitchClarity;
                        // Prefer note-segmentation for melodic material; fall back
                        // to the already-computed transient onsets if it yields
                        // fewer than 2 boundaries (e.g. a single sustained note).
                        auto notes = segmentNotes (*file);
                        result.transients = (notes.size() >= 2)
                            ? std::move (notes)
                            : std::move (onsets);
                    }
                    else if (pitchClarity <= 0.20f && onsetRate < 1.5)
                    {
                        result.classification = SourceClass::Texture;
                        TextureAnalyzer::Params tp;
                        tp.sensitivity    = owner_.detectorParams_.sensitivity;
                        result.transients = TextureAnalyzer { tp }.analyze (*file);
                    }
                    else
                    {
                        result.classification = SourceClass::Percussive;
                        result.transients     = std::move (onsets);
                    }
                    break;
                }

                case AnalysisMode::Percussive:
                default:
                {
                    result.classification = SourceClass::Percussive;
                    TransientDetector det { owner_.detectorParams_ };
                    result.transients = det.detect (*file);
                    break;
                }
            }

            if (shouldExit())
            {
                result.errorMessage = "Cancelled";
                dispatch (std::move (result));
                return jobHasFinished;
            }

            // ── Natural ends (needed before guardrail for accurate RMS windows) ──
            // Use the default -50 dB floor — the stratified guardrail's
            // next-onset gap clamp (20 ms) already prevents overlap between
            // adjacent slices without truncating the tail of single-shot samples.
            if (! result.transients.empty())
                computeNaturalEnds (*file, result.transients, -50.0f);

            // ── Stratified Top-4 curated one-shot guardrail ──────────────────
            // Pure confidence ranking causes the 4 picks to bunch together at
            // the loudest section of long files (e.g. a periodic triangle
            // melody where every ding has similar energy — the detector keeps
            // the first 4 dings and ignores the rest of the file). To get
            // representative coverage we stratify: split the file into 4 equal
            // time bands and pick the highest-confidence onset in each band.
            // Empty bands are filled in by next-best confidence overall.
            //
            // Steps (all on the analysis thread — thread-safe):
            //   1. Snapshot all onset positions (used to clamp slice ends).
            //   2. Bucket onsets into 4 time bands; keep the strongest per band.
            //   3. Backfill empty bands from the highest remaining confidence.
            //   4. For each kept slice: cap end at next-onset minus 20 ms gap,
            //      truncate to 1.5 s, discard if RMS < -45 dB.
            //   5. Re-sort kept slices by time for playback order.
            if (! result.transients.empty())
            {
                constexpr float      kMaxSliceSec  = 1.5f;
                // Adaptive RMS gate: purely relative — discard a slice only if its
                // RMS is below 10 % of the loudest slice in the file (-20 dB).
                // The gate scales with the actual content so quiet material
                // (e.g. a -60 dBFS triangle melody) is never culled by a fixed
                // absolute floor. Only completely silent files (maxRms < -80dBFS)
                // produce 0 slices.
                constexpr float      kAbsoluteRmsFloor = 0.0001f;   // -80 dB safety
                constexpr float      kRelativeRmsRatio = 0.10f;     // -20 dB peak
                constexpr int        kMaxSlices    = 4;
                const std::int64_t   maxSliceSamples =
                    static_cast<std::int64_t> (kMaxSliceSec * file->sampleRate);
                const std::int64_t   totalSamples = file->samples.getNumSamples();
                const std::int64_t   kSliceGap =
                    static_cast<std::int64_t> (0.020 * file->sampleRate); // 20 ms

                // Snapshot all onset positions in temporal order
                std::vector<std::int64_t> allOnsets;
                allOnsets.reserve (result.transients.size());
                for (const auto& t : result.transients)
                    allOnsets.push_back (t.sampleIndex);
                std::sort (allOnsets.begin(), allOnsets.end());

                // ── Stratified pick: best-of-band, then backfill ─────────────
                const std::int64_t bandSize = std::max<std::int64_t> (
                    1, totalSamples / kMaxSlices);

                std::array<int, kMaxSlices> bestPerBand;   // index into transients (-1 = empty)
                bestPerBand.fill (-1);

                for (int i = 0; i < static_cast<int> (result.transients.size()); ++i)
                {
                    const auto& t = result.transients[static_cast<std::size_t> (i)];
                    int band = static_cast<int> (t.sampleIndex / bandSize);
                    if (band < 0)              band = 0;
                    if (band >= kMaxSlices)    band = kMaxSlices - 1;

                    const int prev = bestPerBand[static_cast<std::size_t> (band)];
                    if (prev < 0
                        || t.confidence > result.transients[static_cast<std::size_t> (prev)].confidence)
                    {
                        bestPerBand[static_cast<std::size_t> (band)] = i;
                    }
                }

                std::vector<int> chosen;
                chosen.reserve (kMaxSlices);
                for (int idx : bestPerBand)
                    if (idx >= 0) chosen.push_back (idx);

                // Backfill from highest-confidence remaining if any band was empty
                if (static_cast<int> (chosen.size()) < kMaxSlices
                    && result.transients.size() > chosen.size())
                {
                    std::vector<int> ranked (result.transients.size());
                    std::iota (ranked.begin(), ranked.end(), 0);
                    std::sort (ranked.begin(), ranked.end(),
                        [&] (int a, int b)
                        {
                            return result.transients[static_cast<std::size_t> (a)].confidence
                                 > result.transients[static_cast<std::size_t> (b)].confidence;
                        });

                    for (int idx : ranked)
                    {
                        if (static_cast<int> (chosen.size()) >= kMaxSlices) break;
                        if (std::find (chosen.begin(), chosen.end(), idx) == chosen.end())
                            chosen.push_back (idx);
                    }
                }

                // ── Two-pass: first finalise boundaries + measure RMS, then
                //    derive an adaptive RMS floor from the loudest chosen slice
                //    and apply the gate. This way the gate scales with the
                //    actual loudness of the material instead of dropping every
                //    slice when the whole file sits below a fixed threshold.
                struct Candidate { Transient t; float rms; };
                std::vector<Candidate> finalised;
                finalised.reserve (chosen.size());
                float maxRms = 0.0f;
                for (int idx : chosen)
                {
                    Transient t = result.transients[static_cast<std::size_t> (idx)];

                    std::int64_t rawEnd =
                        (t.naturalEnd > 0) ? t.naturalEnd : totalSamples;

                    for (const auto onset : allOnsets)
                    {
                        if (onset > t.sampleIndex + 1)
                        {
                            const std::int64_t cap = onset - kSliceGap;
                            if (cap > t.sampleIndex && rawEnd > cap)
                                rawEnd = cap;
                            break;
                        }
                    }

                    t.naturalEnd = std::min (rawEnd, t.sampleIndex + maxSliceSamples);

                    const float rms = computeSliceRms (
                        file->samples, t.sampleIndex, t.naturalEnd);

                    maxRms = std::max (maxRms, rms);
                    finalised.push_back ({ t, rms });
                }

                const float adaptiveFloor = std::max (kAbsoluteRmsFloor,
                                                      maxRms * kRelativeRmsRatio);

                std::vector<Transient> kept;
                kept.reserve (finalised.size());
                for (auto& c : finalised)
                    if (c.rms >= adaptiveFloor)
                        kept.push_back (c.t);

                std::sort (kept.begin(), kept.end(),
                    [] (const auto& a, const auto& b) { return a.sampleIndex < b.sampleIndex; });

                result.transients = std::move (kept);
            }

            dispatch (std::move (result));
            return jobHasFinished;
        }

    private:
        void notifyStarted() const
        {
            const int idCopy = id_;
            auto* ownerPtr   = &owner_;

            juce::MessageManager::callAsync ([ownerPtr, idCopy]
            {
                const juce::ScopedLock sl (ownerPtr->callbackLock_);
                if (ownerPtr->onStarted_)
                    ownerPtr->onStarted_ (idCopy);
            });
        }

        void dispatch (AnalysisResult r) const
        {
            const int idCopy = id_;
            auto* ownerPtr   = &owner_;

            {
                const juce::ScopedLock sl (ownerPtr->jobMapLock_);
                ownerPtr->activeJobs_.erase (idCopy);
            }

            juce::MessageManager::callAsync (
                [ownerPtr, res = std::move (r)] () mutable
                {
                    CompletionCallback  cb;
                    AllCompleteCallback allCb;
                    {
                        const juce::ScopedLock sl (ownerPtr->callbackLock_);
                        cb    = ownerPtr->onComplete_;
                        allCb = ownerPtr->onAllComplete_;
                    }
                    if (cb)
                        cb (std::move (res));

                    // Fire all-complete when this was the last in-flight job.
                    // fetch_sub returns the value BEFORE subtraction; == 1 means
                    // the counter just dropped to 0.
                    if (ownerPtr->pendingJobCount_.fetch_sub (
                            1, std::memory_order_acq_rel) == 1)
                    {
                        if (allCb)
                            allCb();
                    }
                });
        }

        AnalysisEngine&       owner_;
        int                   id_;
        std::filesystem::path path_;
        AnalysisMode          mode_;
    };

    //==========================================================================
    //  AnalysisEngine — construction / public API
    //==========================================================================
    AnalysisEngine::AnalysisEngine (int numThreads)
        : pool_ (juce::jmax (1, numThreads))   // compatible with all JUCE 7.x
    {
        formatManager_.registerBasicFormats();
       #if JUCE_WINDOWS
        formatManager_.registerFormat (new juce::WindowsMediaAudioFormat(), false);
       #endif
    }

    AnalysisEngine::~AnalysisEngine()
    {
        pool_.removeAllJobs (true, 5000);
    }

    void AnalysisEngine::setDetectorParams (TransientDetector::Params p)
    {
        detectorParams_ = p;
    }

    void AnalysisEngine::setOnStarted (StartedCallback cb)
    {
        const juce::ScopedLock sl (callbackLock_);
        onStarted_ = std::move (cb);
    }

    void AnalysisEngine::setOnComplete (CompletionCallback cb)
    {
        const juce::ScopedLock sl (callbackLock_);
        onComplete_ = std::move (cb);
    }

    void AnalysisEngine::setOnAllComplete (AllCompleteCallback cb)
    {
        const juce::ScopedLock sl (callbackLock_);
        onAllComplete_ = std::move (cb);
    }

    int AnalysisEngine::enqueue (std::filesystem::path path, AnalysisMode mode)
    {
        const int id = nextJobId_.fetch_add (1, std::memory_order_relaxed);
        pendingJobCount_.fetch_add (1, std::memory_order_relaxed);   // track in-flight
        auto job = std::make_unique<Job> (*this, id, std::move (path), mode);
        {
            const juce::ScopedLock sl (jobMapLock_);
            activeJobs_.emplace (id, job.get());
        }
        pool_.addJob (job.release(), true);
        return id;
    }

    void AnalysisEngine::cancel (int jobId)
    {
        juce::ThreadPoolJob* handle = nullptr;
        {
            const juce::ScopedLock sl (jobMapLock_);
            if (auto it = activeJobs_.find (jobId); it != activeJobs_.end())
                handle = it->second;
        }
        if (handle != nullptr)
            pool_.removeJob (handle, true, 2000);
    }

    void AnalysisEngine::waitForAll()
    {
        pool_.removeAllJobs (false, -1);
    }

    AnalysisResult AnalysisEngine::analyzeSync (
        const std::filesystem::path& path,
        AnalysisMode mode,
        const TransientDetector::Params& params)
    {
        juce::AudioFormatManager fmt;
        fmt.registerBasicFormats();
       #if JUCE_WINDOWS
        fmt.registerFormat (new juce::WindowsMediaAudioFormat(), false);
       #endif

        AnalysisResult result;
        result.path          = path;
        result.requestedMode = mode;

        auto file = loadAudioFile (fmt, path);
        if (! file.has_value())
        {
            result.errorMessage = "Failed to load audio file";
            return result;
        }

        result.sampleRate      = file->sampleRate;
        result.lengthInSamples = file->originalLengthInSamples;

        switch (mode)
        {
            case AnalysisMode::Texture:
            {
                result.classification = SourceClass::Texture;
                TextureAnalyzer::Params tp;
                tp.sensitivity = params.sensitivity;
                result.transients = TextureAnalyzer { tp }.analyze (*file);
                break;
            }
            case AnalysisMode::Melodic:
            {
                result.classification = SourceClass::Melodic;
                result.transients = TransientDetector { params }.detect (*file);
                // Pitch estimation (same logic as async path)
                std::vector<float> mono;
                mixToMono (file->samples, mono);
                PitchDetector pd;
                const std::size_t frameSize = 2048;
                for (std::size_t offset = 0; offset + frameSize <= mono.size();
                     offset += frameSize / 2)
                {
                    auto pr = pd.detect (std::span<const float> (
                        mono.data() + offset, frameSize), file->sampleRate);
                    if (pr.has_value() && pr->clarity > 0.5f)
                    {
                        result.pitchHz = pr->f0Hz;
                        break;
                    }
                }
                break;
            }
            default:
            {
                result.classification = SourceClass::Percussive;
                result.transients = TransientDetector { params }.detect (*file);
                break;
            }
        }
        return result;
    }
} // namespace switchblade::analysis
