#pragma once

#include "Analysis/AudioFile.h"
#include "Analysis/SpectralFlux.h"
#include "Analysis/TransientDetector.h"  // for Transient (shared output type)

#include <span>
#include <vector>

namespace switchblade::analysis
{
    //==========================================================================
    //  TextureAnalyzer — finds the *start* of stable spectral regions.
    //
    //  Algorithm:
    //    1. Per-hop: compute RMS energy + spectral centroid from FFT magnitudes.
    //    2. Centroid stability = 1 - (windowed std-dev of centroid / ref scale).
    //       High stability = steady, pad-like texture. Low = transient/transition.
    //    3. RMS envelope is smoothed with a one-pole filter.
    //    4. A "stable region" begins when:
    //          RMS > rmsFloor   AND   stability > stabilityThreshold
    //       after a preceding unstable or silent frame.
    //    5. Each region start is emitted as a Transient (reusing the shared type
    //       so the rest of the pipeline — zero-snap, JSON, export — is identical).
    //
    //  Unlike TransientDetector which fires on spectral *increases*, Texture
    //  mode fires on *settling*: the moment the spectrum stops changing is when
    //  the sample is most useful for granular synthesis.
    //==========================================================================
    class TextureAnalyzer
    {
    public:
        struct Params
        {
            SpectralFlux::Config spectral      {};
            float rmsFloor            { 0.02f };   // below this = silence, ignore
            float stabilityThreshold  { 0.70f };   // 0..1; higher = stricter
            float stabilityWindowSec  { 0.30f };   // centroid std-dev window
            float minSpacingMs        { 200.0f };  // min gap between region starts
            float sensitivity         { 1.0f };    // < 1 strict, > 1 permissive
        };

        explicit TextureAnalyzer (Params p = {}) noexcept : params_ (p) {}

        /** Same return type as TransientDetector::detect — drop-in replacement. */
        [[nodiscard]] std::vector<Transient> analyze (const AudioFile& file) const;

        [[nodiscard]] const Params& params() const noexcept { return params_; }

    private:
        Params params_;

        static float computeCentroid (std::span<const float> magnitudes) noexcept;
        static float windowedStdDev  (std::span<const float> series) noexcept;
    };
} // namespace switchblade::analysis
