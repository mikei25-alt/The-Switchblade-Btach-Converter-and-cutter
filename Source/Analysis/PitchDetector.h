#pragma once

#include "AudioFile.h"

#include <algorithm>
#include <cmath>
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
            float minHz      {  55.0f };  // A1
            float maxHz      { 2093.0f }; // C7
            float noiseFloor { 0.0056f }; // ~ -45 dBFS — skip detection below this peak
        };

        explicit PitchDetector (Config cfg = {}) noexcept : cfg_ (cfg) {}

        /** Analyse a mono audio frame. sampleRate must match the source. */
        [[nodiscard]] std::optional<PitchResult>
        detect (std::span<const float> frame, double sampleRate) const noexcept;

        /** Convert Hz to a note name string, e.g. "A4", "C#3". */
        [[nodiscard]] static std::string noteNameFromHz (float hz) noexcept;

        /** Return the MIDI note closest to hz (69 = A4 = 440 Hz). */
        [[nodiscard]] static int midiNoteFromHz (float hz) noexcept;

    private:
        Config cfg_;
    };

    //==========================================================================
    //  Inline implementations
    //==========================================================================
    inline std::optional<PitchResult>
    PitchDetector::detect (std::span<const float> frame,
                           double sampleRate) const noexcept
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

        // Step 1 + 2: difference function + cumulative mean normalisation
        std::vector<float> d (static_cast<std::size_t> (searchMax + 1), 0.0f);

        // d[0] = 1 by definition
        d[0] = 1.0f;

        double runningSum = 0.0;
        for (int tau = 1; tau <= searchMax; ++tau)
        {
            double acc = 0.0;
            for (int j = 0; j < W - tau; ++j)
            {
                const double diff = static_cast<double> (frame[static_cast<std::size_t> (j)])
                                  - static_cast<double> (frame[static_cast<std::size_t> (j + tau)]);
                acc += diff * diff;
            }
            runningSum += acc;
            // Cumulative mean normalised difference
            d[static_cast<std::size_t> (tau)] =
                (runningSum > 0.0)
                ? static_cast<float> (acc * static_cast<double> (tau) / runningSum)
                : 1.0f;
        }

        // Step 3: find first tau in [tauMin, searchMax] where d[tau] < threshold
        //         and it is a local minimum (or use absolute minimum if none found)
        int bestTau = -1;
        float bestVal = 1.0f;

        for (int tau = tauMin; tau < searchMax - 1; ++tau)
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
        const int numCh = file.samples.getNumChannels();
        std::vector<float> mono (static_cast<std::size_t> (frameLen));
        for (juce::int64 i = 0; i < frameLen; ++i)
        {
            float sum = 0.0f;
            for (int c = 0; c < numCh; ++c)
                sum += file.samples.getSample (c, static_cast<int> (frameStart + i));
            mono[static_cast<std::size_t> (i)] =
                sum / static_cast<float> (std::max (1, numCh));
        }

        PitchDetector::Config cfg;
        cfg.frameSize = static_cast<int> (frameLen);
        PitchDetector pd (cfg);
        const auto pr = pd.detect (std::span<const float> (mono), file.sampleRate);
        if (pr.has_value() && pr->clarity > 0.5f)
            return pr->f0Hz;
        return std::nullopt;
    }
} // namespace switchblade::analysis
