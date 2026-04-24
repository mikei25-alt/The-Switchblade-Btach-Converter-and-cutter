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
                // Onset is centred on the frame — use frame start + half-window
                // offset to approximate the actual onset in samples.
                t.rawSampleIndex = static_cast<std::int64_t> (i) * hop + fftN / 4;
                t.fluxValue  = v;
                t.confidence = std::clamp (v / std::max (th, 1e-6f) - 1.0f, 0.0f, 1.0f);
                candidates.push_back (t);
                lastAcceptedFrame = i;
            }
        }

        // 4. Zero-crossing snap on the mono source.
        const std::int64_t snapRadius = static_cast<std::int64_t> (
            std::llround ((params_.zeroSnapMs * 1e-3) * sr));

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
