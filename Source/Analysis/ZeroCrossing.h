#pragma once

#include <span>
#include <cstdint>

namespace switchblade::analysis
{
    //==========================================================================
    //  Zero-crossing snap. Given a target sample index and a ±search window,
    //  return the nearest sample where the signal crosses zero (sign change).
    //  If no crossing is found within the window, returns the original index.
    //
    //  Used to align slice points so exported one-shots don't click at start.
    //==========================================================================
    [[nodiscard]] inline std::int64_t snapToZeroCrossing (
        std::span<const float> mono,
        std::int64_t            target,
        std::int64_t            searchRadius) noexcept
    {
        const auto n = static_cast<std::int64_t> (mono.size());
        if (n < 2)
            return target;

        const auto lo = std::max<std::int64_t> (1, target - searchRadius);
        const auto hi = std::min<std::int64_t> (n - 1, target + searchRadius);
        if (lo >= hi)
            return target;

        std::int64_t bestIdx = target;
        std::int64_t bestDist = searchRadius + 1;

        for (std::int64_t i = lo; i <= hi; ++i)
        {
            const float a = mono[static_cast<std::size_t> (i - 1)];
            const float b = mono[static_cast<std::size_t> (i)];
            const bool  crosses = (a <= 0.0f && b > 0.0f) || (a >= 0.0f && b < 0.0f);

            if (crosses)
            {
                const std::int64_t d = std::abs (i - target);
                if (d < bestDist)
                {
                    bestDist = d;
                    bestIdx  = i;
                }
            }
        }
        return bestIdx;
    }
} // namespace switchblade::analysis
