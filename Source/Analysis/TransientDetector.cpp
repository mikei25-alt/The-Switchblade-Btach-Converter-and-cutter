#include "TransientDetector.h"
#include "Analysis/ZeroCrossing.h"

#include <algorithm>
#include <cmath>

namespace switchblade::analysis
{
    std::vector<Transient> TransientDetector::detect (const AudioFile& file) const
    {
        if (! file.isValid())
            return {};

        // 1. Flatten to mono for detection (original remains intact for export).
        std::vector<float> mono;
        mixToMono (file.samples, mono);
        if (mono.empty())
            return {};

        // 2. Run Spectral Flux → novelty curve.
        SpectralFlux flux { params_.spectral };
        const auto novelty = flux.process (mono);
        if (novelty.empty())
            return {};

        const int   hop       = flux.hopSize();
        const int   fftN      = flux.fftSize();
        const double sr       = file.sampleRate;

        // 3. Adaptive-threshold gate + local-maxima peak pick.
        AdaptiveThreshold gate { params_.threshold };
        const auto minSpacingFrames = static_cast<std::size_t> (
            std::max (1.0, (params_.minSpacingMs * 1e-3) * sr / static_cast<double> (hop)));

        std::vector<Transient> candidates;
        candidates.reserve (novelty.size() / 8);

        // Sensitivity acts as an inverse multiplier on the threshold — higher
        // sensitivity means easier to trigger.
        const float sensInv = (params_.sensitivity > 0.0f)
                              ? 1.0f / params_.sensitivity : 1.0f;

        std::size_t lastAcceptedFrame = 0;
        for (std::size_t i = 1; i + 1 < novelty.size(); ++i)
        {
            const float v  = novelty[i];
            const float th = gate (v) * sensInv;

            const bool isLocalMax = (v > novelty[i - 1]) && (v >= novelty[i + 1]);
            const bool aboveTh    = v >= th;
            const bool spacedOk   = (candidates.empty())
                                 || (i - lastAcceptedFrame) >= minSpacingFrames;

            if (isLocalMax && aboveTh && spacedOk)
            {
                Transient t;
                t.rawSampleIndex = static_cast<std::int64_t> (i) * hop + fftN / 2;
                t.fluxValue  = v;
                t.confidence = std::clamp (v / std::max (th, 1e-6f) - 1.0f, 0.0f, 1.0f);
                candidates.push_back (t);
                lastAcceptedFrame = i;
            }
        }

        // 4. Silence gate — drop candidates whose local peak is below -40dB of
        //    the file's overall peak. Prevents false markers in silent passages
        //    (e.g. long-tail reverb, inter-phrase gaps in melodic recordings).
        if (! candidates.empty())
        {
            float filePeak = 0.0f;
            for (float s : mono)
                filePeak = std::max (filePeak, std::abs (s));

            if (filePeak > 1e-6f)
            {
                // -40dBFS relative to the file peak → linear ≈ 0.01
                const float silenceFloor = filePeak * 0.01f;
                // 10ms window starting at the raw onset sample
                const std::size_t checkLen = static_cast<std::size_t> (sr * 0.010);

                candidates.erase (
                    std::remove_if (candidates.begin(), candidates.end(),
                        [&] (const Transient& t)
                        {
                            const std::size_t si = static_cast<std::size_t> (
                                std::max (std::int64_t { 0 }, t.rawSampleIndex));
                            const std::size_t end = std::min (si + checkLen, mono.size());
                            float localPeak = 0.0f;
                            for (std::size_t n = si; n < end; ++n)
                                localPeak = std::max (localPeak, std::abs (mono[n]));
                            return localPeak < silenceFloor;
                        }),
                    candidates.end());
            }
        }

        // 5. Zero-crossing snap on the mono source.
        const std::int64_t snapRadius = static_cast<std::int64_t> (
            std::llround ((params_.zeroSnapMs * 1e-3) * sr));

        // Fallback for single-shot files that start immediately with content:
        // spectral flux for frame 0 is always compared against prevMag=0 and
        // is never checkable as a local max (loop starts at i=1). If no onsets
        // were found but the first two hops have significant RMS energy, the
        // file is a one-shot starting at sample 0 — synthesise a transient there.
        if (candidates.empty())
        {
            const std::int64_t checkEnd = std::min (
                static_cast<std::int64_t> (hop * 2),
                static_cast<std::int64_t> (mono.size()));
            double sumSq = 0.0;
            for (std::int64_t si = 0; si < checkEnd; ++si)
                sumSq += static_cast<double> (mono[static_cast<std::size_t> (si)])
                       * static_cast<double> (mono[static_cast<std::size_t> (si)]);
            const float earlyRms = static_cast<float> (
                std::sqrt (sumSq / static_cast<double> (checkEnd)));

            if (earlyRms > 0.001f)   // above -60 dBFS — not digital silence
            {
                Transient t;
                t.rawSampleIndex = 0;
                t.sampleIndex    = snapToZeroCrossing (
                    std::span<const float> (mono.data(), mono.size()),
                    0, snapRadius);
                t.timeSeconds    = static_cast<double> (t.sampleIndex) / sr;
                t.fluxValue      = earlyRms;
                t.confidence     = 0.3f;
                candidates.push_back (t);
            }
        }

        for (auto& t : candidates)
        {
            t.sampleIndex = snapToZeroCrossing (
                std::span<const float> (mono.data(), mono.size()),
                t.rawSampleIndex, snapRadius);
            t.timeSeconds = static_cast<double> (t.sampleIndex) / sr;
        }

        return candidates;
    }
} // namespace switchblade::analysis
