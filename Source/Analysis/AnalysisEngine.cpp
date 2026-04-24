#include "AnalysisEngine.h"
#include "Analysis/TextureAnalyzer.h"
#include "Analysis/PitchDetector.h"
#include "Analysis/SliceBoundary.h"
#include "Analysis/NoteSegmenter.h"

#include <utility>

namespace switchblade::analysis
{
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
                                    if (pitchClarity > 0.60f)
                                        break;
                                }
                            }
                        }
                    }

                    if (pitchClarity > 0.60f && onsetRate < 5.0)
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

            // ── Density Guard ────────────────────────────────────────────────
            // Runs before computeNaturalEnds so we only process the final set.

            // Percussive: cap at 8 slices (or 4/sec for longer loops)
            if (result.classification == SourceClass::Percussive && result.transients.size() > 8)
            {
                const double durationSec = (file->sampleRate > 0.0 && file->originalLengthInSamples > 0)
                    ? static_cast<double> (file->originalLengthInSamples) / file->sampleRate
                    : 0.0;
                const double maxSlices = std::max (8.0, std::ceil (durationSec * 4.0));
                const std::size_t targetCount = static_cast<std::size_t> (
                    std::min (static_cast<double> (result.transients.size()), maxSlices));

                if (targetCount < result.transients.size())
                {
                    std::sort (result.transients.begin(), result.transients.end(),
                        [] (const auto& a, const auto& b) { return a.confidence > b.confidence; });
                    result.transients.resize (targetCount);
                    std::sort (result.transients.begin(), result.transients.end(),
                        [] (const auto& a, const auto& b) { return a.sampleIndex < b.sampleIndex; });
                }
            }

            // Melodic: hard cap at 4 slices, kept in temporal order.
            // Note segmenter already returns notes in time order; we trim the tail.
            if (result.classification == SourceClass::Melodic && result.transients.size() > 4)
                result.transients.resize (4);

            // ── Fill in energy-based natural ends for every transient ────────
            if (! result.transients.empty())
                computeNaturalEnds (*file, result.transients);

            // ── Melodic slice duration cap: 2 seconds max per slice ──────────
            // Melodic export filename carries the detected pitch badge (e.g. _A4_);
            // pitchHz is stored on result and consumed by MainContainer export.
            // Slice length is capped here so exported samples are 1-2s usable hits.
            if (result.classification == SourceClass::Melodic && ! result.transients.empty())
            {
                const std::int64_t maxSliceSamples =
                    static_cast<std::int64_t> (2.0 * file->sampleRate);
                for (auto& t : result.transients)
                {
                    const std::int64_t cap = t.sampleIndex + maxSliceSamples;
                    if (t.naturalEnd <= 0 || t.naturalEnd > cap)
                        t.naturalEnd = cap;
                }
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
