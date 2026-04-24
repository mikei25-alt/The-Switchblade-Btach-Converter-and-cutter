#pragma once

#include "Analysis/AudioFile.h"
#include "Analysis/SpectralFlux.h"
#include "Analysis/AdaptiveThreshold.h"

#include <cstdint>
#include <vector>

namespace switchblade::analysis
{
    //==========================================================================
    //  TransientDetector — combines SpectralFlux + AdaptiveThreshold +
    //  peak-picking + zero-crossing snap to produce a list of slice points.
    //
    //  This is the "Percussive / Transient Mode" branch of the Switchblade
    //  Brain. The Texture-Mode and Auto-Mode branches live in their own
    //  classes (TextureAnalyzer, ClassifierBridge) and compose through the
    //  AnalysisEngine façade.
    //==========================================================================
    struct Transient
    {
        std::int64_t sampleIndex    { 0 };     // snapped to zero crossing
        std::int64_t rawSampleIndex { 0 };     // pre-snap (debug / tuning)
        std::int64_t naturalEnd     { 0 };     // energy-decay endpoint (filled by
                                               // computeNaturalEnds in SliceBoundary.h).
                                               // 0 = not yet computed; fall back to
                                               // the next onset or file end.
        double       timeSeconds    { 0.0 };
        float        fluxValue      { 0.0f };
        float        confidence     { 0.0f };  // 0..1 — flux / threshold ratio
    };

    class TransientDetector
    {
    public:
        struct Params
        {
            SpectralFlux::Config       spectral       {};
            AdaptiveThreshold::Params  threshold      {};
            float                      minSpacingMs   { 100.0f }; // refractory period — no two onsets within 100ms
            float                      zeroSnapMs     { 5.0f };   // ± search radius
            float                      sensitivity    { 0.7f };   // 0.5 = strict, 2.0 = loose — lower for fewer false positives
        };

        explicit TransientDetector (Params p = {}) noexcept : params_ (p) {}

        [[nodiscard]] std::vector<Transient> detect (const AudioFile& file) const;

        [[nodiscard]] const Params& params() const noexcept { return params_; }

    private:
        Params params_;
    };
} // namespace switchblade::analysis
