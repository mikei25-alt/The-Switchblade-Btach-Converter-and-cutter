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
    //  Complexity: O(W log W) per sample for simplicity. For real-time loops
    //  with very long windows, swap in an order-statistic tree; for analysis
    //  pass (offline), this is fine and keeps the code obvious.
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

        explicit AdaptiveThreshold (Params p = {})
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

            // Reuse the pre-allocated scratch buffer — no heap alloc per call.
            // Two sorts on n ≤ 65 elements; sort is the bottleneck, not the alloc.
            std::copy (window_.begin(), window_.end(), scratch_.begin());
            std::sort (scratch_.begin(), scratch_.begin() + n);
            const float median = scratch_[n / 2];

            // Reuse the same buffer for MAD deviations (overwrite in-place).
            for (std::size_t i = 0; i < n; ++i)
                scratch_[i] = std::abs (scratch_[i] - median);
            std::sort (scratch_.begin(), scratch_.begin() + n);
            const float mad = scratch_[n / 2];

            return std::max (params_.floorAbs,
                             median + params_.k * 1.4826f * mad);
        }

        Params                    params_;
        std::deque<float>         window_;
        mutable std::vector<float> scratch_; // pre-sized to windowSize at construction
    };
} // namespace switchblade::analysis
