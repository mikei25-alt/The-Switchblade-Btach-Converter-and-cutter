#pragma once

#include <algorithm>
#include <deque>
#include <vector>

namespace switchblade::analysis
{
    //==========================================================================
    //  AdaptiveThreshold
    //
    //  Sliding-window median + MAD (Median Absolute Deviation) based threshold
    //  used to gate the Spectral Flux novelty function. MAD is robust against
    //  outliers (i.e. the very peaks we're trying to detect), unlike mean/std.
    //
    //      threshold(n) = median(w) + k * 1.4826 * MAD(w) + floor
    //
    //  where 1.4826 scales MAD to be a consistent estimator of std-dev under
    //  Gaussian noise. Use `k ≈ 1.5 .. 3.0` — higher = fewer false positives.
    //
    //  Complexity: O(W) per sample — both order statistics come from
    //  std::nth_element on the pre-allocated scratch buffer instead of the
    //  two full std::sort passes used previously. Output is value-identical:
    //  the selected elements (index n/2 of the sorted values, then of the
    //  sorted deviations) are the same order statistics either way.
    //==========================================================================
    class AdaptiveThreshold
    {
    public:
        struct Params
        {
            std::size_t windowSize { 65 };      // ~750ms at 11.6ms hop — wider window smooths ghost triggers
            float       k          { 2.4f };    // MAD multiplier — slightly relaxed so the gate stays responsive across long files
            float       floorAbs   { 0.008f };  // absolute minimum — low enough to catch quiet material (e.g. -60 dBFS triangle dings) while still sitting above digital noise
        };

        AdaptiveThreshold() : AdaptiveThreshold (Params()) {}
        explicit AdaptiveThreshold (Params p)
            : params_ (p), scratch_ (p.windowSize) {}

        void reset() noexcept { window_.clear(); }

        /** Push a new novelty value and return the threshold *before* it was
            added (so `value >= returned` is a valid gate). */
        [[nodiscard]] float operator() (float value)
        {
            const float t = compute();
            window_.push_back (value);
            if (window_.size() > params_.windowSize)
                window_.pop_front();
            return t;
        }

    private:
        [[nodiscard]] float compute() const
        {
            const std::size_t n = window_.size();
            if (n < 4)
                return params_.floorAbs;

            // Median via selection, not a full sort.
            std::copy (window_.begin(), window_.end(), scratch_.begin());
            const auto mid = scratch_.begin() + static_cast<std::ptrdiff_t> (n / 2);
            std::nth_element (scratch_.begin(), mid, scratch_.begin() + static_cast<std::ptrdiff_t> (n));
            const float median = *mid;

            // MAD: median of the absolute deviations (same buffer, in place).
            for (std::size_t i = 0; i < n; ++i)
                scratch_[i] = std::abs (scratch_[i] - median);
            std::nth_element (scratch_.begin(), mid, scratch_.begin() + static_cast<std::ptrdiff_t> (n));
            const float mad = *mid;

            return std::max (params_.floorAbs,
                             median + params_.k * 1.4826f * mad);
        }

        Params                     params_;
        std::deque<float>          window_;
        mutable std::vector<float> scratch_; // pre-sized to windowSize at construction
    };
} // namespace switchblade::analysis
