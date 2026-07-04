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

        const auto crossesAt = [&] (std::int64_t i) noexcept
        {
            const float a = mono[static_cast<std::size_t> (i - 1)];
            const float b = mono[static_cast<std::size_t> (i)];
            return (a <= 0.0f && b > 0.0f) || (a >= 0.0f && b < 0.0f);
        };

        // Walk outward from the target and stop at the first crossing — audio
        // crosses zero every few dozen samples, so this exits far earlier than
        // scanning the whole ±radius window. Left side first on equal distance
        // (matches the previous full-scan tie-break). The explicit bound also
        // covers a target outside [lo, hi] (e.g. target 0 with lo clamped to 1).
        const std::int64_t maxDist = std::max (target - lo, hi - target);
        for (std::int64_t dist = 0; dist <= maxDist; ++dist)
        {
            const std::int64_t left = target - dist;
            if (left >= lo && left <= hi && crossesAt (left))
                return left;

            const std::int64_t right = target + dist;
            if (dist > 0 && right >= lo && right <= hi && crossesAt (right))
                return right;
        }
        return target;
    }
} // namespace switchblade::analysis
