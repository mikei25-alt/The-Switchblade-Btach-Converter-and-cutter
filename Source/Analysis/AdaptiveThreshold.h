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
            std::size_t windowSize { 43 };      // ~500ms at 11.6ms hop
            float       k          { 2.8f };    // MAD multiplier — raised to reduce false positives
            float       floorAbs   { 0.08f };   // absolute minimum — raised to suppress low-energy noise
        };

        explicit AdaptiveThreshold (Params p = {}) noexcept : params_ (p) {}

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
            if (window_.size() < 4)
                return params_.floorAbs;

            std::vector<float> sorted (window_.begin(), window_.end());
            std::sort (sorted.begin(), sorted.end());
            const float median = sorted[sorted.size() / 2];

            std::vector<float> devs (sorted.size());
            for (std::size_t i = 0; i < sorted.size(); ++i)
                devs[i] = std::abs (sorted[i] - median);
            std::sort (devs.begin(), devs.end());
            const float mad = devs[devs.size() / 2];

            return std::max (params_.floorAbs,
                             median + params_.k * 1.4826f * mad);
        }

        Params              params_;
        std::deque<float>   window_;
    };
} // namespace switchblade::analysis
