#pragma once

#include <algorithm>
#include <cmath>

namespace switchblade::analysis
{
    //==========================================================================
    //  SliceFades — single source of truth for the anti-click fade profile
    //  applied to slice playback and slice rendering.
    //
    //  Two call sites use this:
    //    1. renderSliceToWav (export / drag-to-DAW temp files)
    //    2. GridVoiceBank::getNextAudioBlock (in-app preview)
    //
    //  Keeping the profile shared guarantees that what the user hears in the
    //  Vault preview is exactly what the DAW receives on import.
    //
    //  Lengths are capped to sliceLen / 4 so a 30 ms tail can't eat half a
    //  20 ms slice — for very short hits the cap kicks in and both ends scale
    //  proportionally with the slice length.
    //==========================================================================
    constexpr double kFadeInMs  =  5.0;
    constexpr double kFadeOutMs = 30.0;

    struct FadeProfile
    {
        int fadeInSamples  { 0 };
        int fadeOutSamples { 0 };
    };

    [[nodiscard]] inline FadeProfile sliceFades (int sliceLen, double sampleRate) noexcept
    {
        if (sliceLen <= 0 || sampleRate <= 0.0)
            return {};

        const int wantedIn  = static_cast<int> (std::round (kFadeInMs  * 0.001 * sampleRate));
        const int wantedOut = static_cast<int> (std::round (kFadeOutMs * 0.001 * sampleRate));
        const int cap       = std::max (0, sliceLen / 4);

        return {
            std::min (wantedIn,  cap),
            std::min (wantedOut, cap),
        };
    }
} // namespace switchblade::analysis
