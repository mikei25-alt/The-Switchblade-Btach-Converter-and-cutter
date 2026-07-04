#pragma once

#include "AudioFile.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace switchblade::analysis
{
    //==========================================================================
    //  PitchDetector — YIN algorithm (de Cheveigné & Kawahara, 2002).
    //
    //  YIN finds the fundamental frequency of a pitched signal by searching
    //  for the lag tau that minimises the normalised difference function:
    //
    //      d'(tau) = d(tau) / [(1/tau) * Σ_{j=1}^{tau} d(j)]
    //
    //  where d(tau) = Σ (x[t] - x[t+tau])^2.
    //
    //  The algorithm is applied to a single mono analysis frame. For a batch
    //  estimate of a whole file, the caller should extract a representative
    //  frame (e.g. the sustain region found by TextureAnalyzer).
    //
    //  Returns nullopt when the signal is unpitched (all d' values above the
    //  threshold, or no valid minimum found in the search range).
    //==========================================================================
    struct PitchResult
    {
        float f0Hz    { 0.0f };   // fundamental frequency
        float clarity { 0.0f };   // 1 - d'(tauOpt); closer to 1 = more confident
    };

    class PitchDetector
    {
    public:
        struct Config
        {
            int   frameSize  { 4096 };    // larger window for better low-end resolution
            float threshold  { 0.10f };   // typical YIN: 0.1 .. 0.15
            float minHz      {  55.0f };  // A1 — kept tight so the file-wide scans (small frames) stay fast.
                                          // detectSlicePitchHz overrides this locally for bass-friendly per-slice labeling.
            float maxHz      { 2093.0f }; // C7
            float noiseFloor { 0.0056f }; // ~ -45 dBFS — skip detection below this peak
        };

        PitchDetector() noexcept : PitchDetector (Config()) {}
        explicit PitchDetector (Config cfg) noexcept : cfg_ (cfg) {}

        /** Analyse a mono audio frame. sampleRate must match the source.
            Non-const: reuses the internal FFT workspace across calls, so a
            PitchDetector instance must not be shared between threads — every
            call site constructs its own local instance. */
        [[nodiscard]] std::optional<PitchResult>
        detect (std::span<const float> frame, double sampleRate) noexcept;

        /** Convert Hz to a note name string, e.g. "A4", "C#3". */
        [[nodiscard]] static std::string noteNameFromHz (float hz) noexcept;

        /** Return the MIDI note closest to hz (69 = A4 = 440 Hz). */
        [[nodiscard]] static int midiNoteFromHz (float hz) noexcept;

        /** Signed cents deviation from the nearest equal-tempered semitone.
            Range [-50, +50]. Returns 0 for hz <= 0. */
        [[nodiscard]] static int centsOffsetFromHz (float hz) noexcept;

        /** Note name with a signed, 2-digit cents suffix, e.g. "A4+12c",
            "C#3-08c". The suffix is omitted when the offset rounds to zero,
            so a perfectly-tuned slice still reads as just "C3". Returns "?"
            for hz <= 0. */
        [[nodiscard]] static std::string noteNameWithCentsFromHz (float hz) noexcept;

    private:
        Config cfg_;

        // FFT workspace for the O(W log W) difference function. Lazily built,
        // reused across detect() calls of the same size (the common case for
        // file-wide scans).
        std::unique_ptr<juce::dsp::FFT> fft_;
        int                             fftOrder_ { -1 };
        std::vector<float>              fftBuf_;

        // Exact double-precision d(tau) for a single lag — used by the small-
        // frame path and to re-derive the dip values before interpolation.
        [[nodiscard]] static double exactDifference (const float* x, int W,
                                                     int tau) noexcept
        {
            double acc = 0.0;
            for (int j = 0; j < W - tau; ++j)
            {
                const double diff = static_cast<double> (x[j])
                                  - static_cast<double> (x[j + tau]);
                acc += diff * diff;
            }
            return acc;
        }

        // d(tau) for all lags via FFT autocorrelation:
        //     d(tau) = P(tau) + Q(tau) - 2 R(tau)
        // where P/Q are prefix-sum energies of the two overlapping segments and
        // R is the (linear) autocorrelation, computed with one zero-padded
        // real FFT round trip. O(W log W) instead of O(W * searchMax).
        void computeDifferenceFft (const float* x, int W, int searchMax,
                                   std::vector<float>& dRaw) noexcept
        {
            // Prefix energies in double — cheap and immune to cancellation.
            std::vector<double> energy (static_cast<std::size_t> (W + 1), 0.0);
            for (int j = 0; j < W; ++j)
                energy[static_cast<std::size_t> (j + 1)] =
                    energy[static_cast<std::size_t> (j)]
                    + static_cast<double> (x[j]) * static_cast<double> (x[j]);

            // Zero-pad past W + searchMax so the circular autocorrelation is
            // linear for every lag we read.
            const int needed = W + searchMax + 1;
            int order = 1;
            while ((1 << order) < needed)
                ++order;
            if (fftOrder_ != order)
            {
                fft_      = std::make_unique<juce::dsp::FFT> (order);
                fftOrder_ = order;
            }
            const int N = 1 << order;

            fftBuf_.assign (static_cast<std::size_t> (2 * N), 0.0f);
            std::copy (x, x + W, fftBuf_.begin());

            fft_->performRealOnlyForwardTransform (fftBuf_.data());
            for (int k = 0; k < N; ++k)
            {
                const float re = fftBuf_[static_cast<std::size_t> (2 * k)];
                const float im = fftBuf_[static_cast<std::size_t> (2 * k + 1)];
                fftBuf_[static_cast<std::size_t> (2 * k)]     = re * re + im * im;
                fftBuf_[static_cast<std::size_t> (2 * k + 1)] = 0.0f;
            }
            fft_->performRealOnlyInverseTransform (fftBuf_.data());

            // Normalise against the exact R(0) = total energy, which absorbs
            // whatever scaling convention the FFT backend uses.
            const double r0 = static_cast<double> (fftBuf_[0]);
            const double scale = (r0 > 0.0)
                ? energy[static_cast<std::size_t> (W)] / r0 : 0.0;

            dRaw[0] = 0.0f;
            const double totalEnergy = energy[static_cast<std::size_t> (W)];
            for (int tau = 1; tau <= searchMax; ++tau)
            {
                const double p = energy[static_cast<std::size_t> (W - tau)];
                const double q = totalEnergy - energy[static_cast<std::size_t> (tau)];
                const double r = scale * static_cast<double> (fftBuf_[static_cast<std::size_t> (tau)]);
                dRaw[static_cast<std::size_t> (tau)] =
                    static_cast<float> (std::max (0.0, p + q - 2.0 * r));
            }
        }
    };

    //==========================================================================
    //  Inline implementations
    //==========================================================================
    inline std::optional<PitchResult>
    PitchDetector::detect (std::span<const float> frame,
                           double sampleRate) noexcept
    {
        const int W = std::min (cfg_.frameSize, static_cast<int> (frame.size()));
        if (W < 4)
            return std::nullopt;

        // Noise floor — if the frame is essentially silent, skip pitch detection
        // entirely. Avoids labelling near-silent slices with spurious low notes.
        {
            float peak = 0.0f;
            for (int i = 0; i < W; ++i)
                peak = std::max (peak, std::abs (frame[static_cast<std::size_t> (i)]));
            if (peak < cfg_.noiseFloor)
                return std::nullopt;
        }

        const int tauMin = static_cast<int> (std::floor (sampleRate / cfg_.maxHz));
        const int tauMax = static_cast<int> (std::ceil  (sampleRate / cfg_.minHz));
        const int searchMax = std::min (tauMax, W / 2 - 1);
        if (tauMin >= searchMax)
            return std::nullopt;

        // Step 1: difference function. Direct evaluation costs W * searchMax
        // multiplies; the FFT route costs ~3 transforms of the padded size.
        // Below ~128k direct operations the FFT setup isn't worth it.
        std::vector<float> dRaw (static_cast<std::size_t> (searchMax + 1), 0.0f);
        const bool useFft =
            static_cast<double> (W) * static_cast<double> (searchMax) >= 131072.0;

        if (useFft)
        {
            computeDifferenceFft (frame.data(), W, searchMax, dRaw);
        }
        else
        {
            for (int tau = 1; tau <= searchMax; ++tau)
                dRaw[static_cast<std::size_t> (tau)] = static_cast<float> (
                    exactDifference (frame.data(), W, tau));
        }

        // Step 2: cumulative mean normalisation
        std::vector<float> d (static_cast<std::size_t> (searchMax + 1), 0.0f);

        // d[0] = 1 by definition
        d[0] = 1.0f;

        double runningSum = 0.0;
        for (int tau = 1; tau <= searchMax; ++tau)
        {
            runningSum += static_cast<double> (dRaw[static_cast<std::size_t> (tau)]);
            d[static_cast<std::size_t> (tau)] =
                (runningSum > 0.0)
                ? static_cast<float> (static_cast<double> (dRaw[static_cast<std::size_t> (tau)])
                                      * static_cast<double> (tau) / runningSum)
                : 1.0f;
        }

        // Step 3: find first tau in [tauMin, searchMax] where d[tau] < threshold
        //         and it is a local minimum (or use absolute minimum if none found)
        int bestTau = -1;
        float bestVal = 1.0f;

        // d[] has size searchMax + 1, so accessing d[tau+1] at tau = searchMax - 1
        // is still in bounds. The previous bound (searchMax - 1) excluded the two
        // lowest-frequency candidates, which mattered for material near minHz.
        for (int tau = tauMin; tau < searchMax; ++tau)
        {
            const float v = d[static_cast<std::size_t> (tau)];
            if (v < cfg_.threshold
                && v < d[static_cast<std::size_t> (tau - 1)]
                && v <= d[static_cast<std::size_t> (tau + 1)])
            {
                bestTau = tau;
                bestVal = v;
                break;
            }
        }

        // Absolute minimum fallback
        if (bestTau < 0)
        {
            for (int tau = tauMin; tau <= searchMax; ++tau)
            {
                const float v = d[static_cast<std::size_t> (tau)];
                if (v < bestVal) { bestVal = v; bestTau = tau; }
            }
            if (bestVal >= 0.5f) // hopeless — unpitched
                return std::nullopt;
        }

        // FFT float round-off is amplified by cancellation right at the dip,
        // where d(tau) is nearly zero. Re-derive the three dip values with the
        // exact double-precision difference so the parabolic interpolation
        // (and the clarity value) keep their full sub-sample precision.
        if (useFft && bestTau > 0 && bestTau < searchMax)
        {
            // Rebuild the cumulative sums for the three dip lags in one pass.
            double cum = 0.0;
            for (int tau = 1; tau <= bestTau + 1; ++tau)
            {
                cum += static_cast<double> (dRaw[static_cast<std::size_t> (tau)]);
                if (tau < bestTau - 1)
                    continue;
                const double exact = exactDifference (frame.data(), W, tau);
                d[static_cast<std::size_t> (tau)] =
                    (cum > 0.0)
                    ? static_cast<float> (exact * static_cast<double> (tau) / cum)
                    : 1.0f;
            }
            bestVal = d[static_cast<std::size_t> (bestTau)];
        }

        // Step 4: parabolic interpolation around bestTau
        float refinedTau = static_cast<float> (bestTau);
        if (bestTau > tauMin && bestTau < searchMax)
        {
            const float x0 = d[static_cast<std::size_t> (bestTau - 1)];
            const float x1 = d[static_cast<std::size_t> (bestTau)];
            const float x2 = d[static_cast<std::size_t> (bestTau + 1)];
            const float denom = 2.0f * (x0 - 2.0f * x1 + x2);
            if (std::abs (denom) > 1e-7f)
                refinedTau += (x0 - x2) / denom;
        }

        if (refinedTau < 1.0f)
            return std::nullopt;

        PitchResult r;
        r.f0Hz    = static_cast<float> (sampleRate) / refinedTau;
        r.clarity = std::clamp (1.0f - bestVal, 0.0f, 1.0f);
        return r;
    }

    inline std::string PitchDetector::noteNameFromHz (float hz) noexcept
    {
        if (hz <= 0.0f) return "?";
        constexpr std::array<const char*, 12> names {
            "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        // Clamp to MIDI [0,127] so the octave/pc arithmetic never overflows
        const int midi   = std::clamp (midiNoteFromHz (hz), 0, 127);
        const int octave = midi / 12 - 1;
        const int pc     = midi % 12;
        return std::string (names[static_cast<std::size_t> (pc)])
             + std::to_string (octave);
    }

    inline int PitchDetector::midiNoteFromHz (float hz) noexcept
    {
        if (hz <= 0.0f) return 0;
        return static_cast<int> (
            std::round (69.0f + 12.0f * std::log2 (hz / 440.0f)));
    }

    inline int PitchDetector::centsOffsetFromHz (float hz) noexcept
    {
        if (hz <= 0.0f) return 0;
        const float midiF = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const float nearest = std::round (midiF);
        return static_cast<int> (std::round ((midiF - nearest) * 100.0f));
    }

    inline std::string PitchDetector::noteNameWithCentsFromHz (float hz) noexcept
    {
        std::string note = noteNameFromHz (hz);
        if (note == "?") return note;

        const int cents = centsOffsetFromHz (hz);
        if (cents == 0) return note;

        const int abs = std::abs (cents);
        note.push_back (cents > 0 ? '+' : '-');
        if (abs < 10) note.push_back ('0');
        note += std::to_string (abs);
        note.push_back ('c');
        return note;
    }

    //==========================================================================
    //  detectSlicePitchHz — runs YIN on a specific [start, end) region of an
    //  AudioFile. Skips past the attack into the sustain region for stable
    //  detection. Returns nullopt for slices that are too short, too quiet,
    //  or unpitched. Used by both the export pipeline (filename / ACID
    //  metadata) and the vault display (per-tile note badges).
    //==========================================================================
    [[nodiscard]] inline std::optional<float> detectSlicePitchHz (
        const AudioFile& file,
        juce::int64 start, juce::int64 end) noexcept
    {
        const juce::int64 totalLen = file.samples.getNumSamples();
        start = std::max<juce::int64> (0, start);
        end   = std::min (end, totalLen);
        if (end <= start) return std::nullopt;

        const juce::int64 sliceLen   = end - start;
        constexpr juce::int64 kMaxFr = 8192;
        constexpr juce::int64 kMinFr = 1024;
        if (sliceLen < kMinFr) return std::nullopt;

        // Skip past the attack — start ~10% in (or 256 samples min), capped so
        // we still have a usable frame remaining in the sustain region.
        const juce::int64 attack    = std::max<juce::int64> (256, sliceLen / 10);
        const juce::int64 frameLen  = std::min (kMaxFr, sliceLen - attack);
        if (frameLen < kMinFr) return std::nullopt;
        const juce::int64 frameStart = start + attack;

        // Mix to mono for analysis
        const int numCh = std::max (1, file.samples.getNumChannels());
        const float gain = 1.0f / static_cast<float> (numCh);
        std::vector<float> mono (static_cast<std::size_t> (frameLen), 0.0f);
        for (int c = 0; c < numCh; ++c)
            juce::FloatVectorOperations::addWithMultiply (
                mono.data(),
                file.samples.getReadPointer (c) + frameStart,
                gain, static_cast<int> (frameLen));

        PitchDetector::Config cfg;
        cfg.frameSize = static_cast<int> (frameLen);
        // Per-slice path uses a much larger frame (up to 8192), which has
        // enough cycles to resolve sub-55 Hz fundamentals — bass low E
        // (41 Hz), upright bass, sub-bass synths. Widening minHz here only
        // costs us cycles where the resolution genuinely warrants it.
        cfg.minHz = 30.0f;
        PitchDetector pd (cfg);
        const auto pr = pd.detect (std::span<const float> (mono), file.sampleRate);
        if (pr.has_value() && pr->clarity > 0.5f)
            return pr->f0Hz;
        return std::nullopt;
    }
} // namespace switchblade::analysis
