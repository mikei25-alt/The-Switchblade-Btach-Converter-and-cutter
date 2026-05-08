#include "TextureAnalyzer.h"
#include "Analysis/ZeroCrossing.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace switchblade::analysis
{
    float TextureAnalyzer::computeCentroid (std::span<const float> mags) noexcept
    {
        float weightedSum = 0.0f;
        float totalMag    = 0.0f;
        for (std::size_t k = 0; k < mags.size(); ++k)
        {
            weightedSum += static_cast<float> (k) * mags[k];
            totalMag    += mags[k];
        }
        return (totalMag > 1e-9f) ? (weightedSum / totalMag) : 0.0f;
    }

    float TextureAnalyzer::windowedStdDev (std::span<const float> series) noexcept
    {
        if (series.size() < 2)
            return 0.0f;
        const float n    = static_cast<float> (series.size());
        const float mean = std::accumulate (series.begin(), series.end(), 0.0f) / n;
        float var = 0.0f;
        for (const auto v : series)
            var += (v - mean) * (v - mean);
        return std::sqrt (var / n);
    }

    // Ring-buffer sliding variance: O(1) push, no erase/shift.
    // Replaces the std::vector::erase(begin()) approach that was O(window) per frame.
    class RingVariance
    {
    public:
        explicit RingVariance (int capacity) noexcept
            : cap_ (capacity), buf_ (static_cast<std::size_t> (capacity), 0.0f) {}

        float push (float x) noexcept
        {
            if (count_ == cap_)
            {
                const float old = buf_[static_cast<std::size_t> (head_)];
                sum_   -= old;
                sumSq_ -= old * old;
                --count_;
            }
            buf_[static_cast<std::size_t> (head_)] = x;
            head_ = (head_ + 1) % cap_;
            sum_   += x;
            sumSq_ += x * x;
            ++count_;
            if (count_ < 2) return 0.0f;
            const float n    = static_cast<float> (count_);
            const float mean = sum_ / n;
            return std::sqrt (std::max (0.0f, sumSq_ / n - mean * mean));
        }

        void reset() noexcept { head_ = count_ = 0; sum_ = sumSq_ = 0.0f; }

    private:
        int   cap_;
        std::vector<float> buf_;
        int   head_  = 0;
        int   count_ = 0;
        float sum_   = 0.0f;
        float sumSq_ = 0.0f;
    };

    std::vector<Transient> TextureAnalyzer::analyze (const AudioFile& file) const
    {
        if (! file.isValid())
            return {};

        std::vector<float> mono;
        mixToMono (file.samples, mono);
        if (mono.empty())
            return {};

        const double sr  = file.sampleRate;
        SpectralFlux fluxCalc { params_.spectral };
        const int fftN = fluxCalc.fftSize();
        const int hop  = fluxCalc.hopSize();

        // ---- Build per-frame (RMS, centroid) curves ----------------------
        const std::size_t totalSamples = mono.size();
        if (static_cast<std::size_t> (fftN) > totalSamples)
            return {};

        const std::size_t numFrames =
            1 + (totalSamples - static_cast<std::size_t> (fftN))
              / static_cast<std::size_t> (hop);

        juce::dsp::FFT fft { params_.spectral.fftOrder };
        juce::dsp::WindowingFunction<float> window {
            static_cast<std::size_t> (fftN),
            juce::dsp::WindowingFunction<float>::hann };

        std::vector<float> scratch (static_cast<std::size_t> (2 * fftN), 0.0f);
        std::vector<float> mags    (static_cast<std::size_t> (fftN / 2), 0.0f);
        std::vector<float> rmsVec  (numFrames);
        std::vector<float> centVec (numFrames);

        for (std::size_t f = 0; f < numFrames; ++f)
        {
            const std::size_t start = f * static_cast<std::size_t> (hop);

            // RMS of raw frame
            float rmsAcc = 0.0f;
            for (int i = 0; i < fftN; ++i)
            {
                const float s = mono[start + static_cast<std::size_t> (i)];
                rmsAcc += s * s;
                scratch[static_cast<std::size_t> (i)] = s;
            }
            std::fill (scratch.begin() + fftN, scratch.end(), 0.0f);
            rmsVec[f] = std::sqrt (rmsAcc / static_cast<float> (fftN));

            // Centroid from magnitude spectrum
            window.multiplyWithWindowingTable (scratch.data(),
                                               static_cast<std::size_t> (fftN));
            fft.performFrequencyOnlyForwardTransform (scratch.data());
            for (int k = 0; k < fftN / 2; ++k)
                mags[static_cast<std::size_t> (k)] =
                    scratch[static_cast<std::size_t> (k)];

            centVec[f] = computeCentroid (
                std::span<const float> (mags.data(), static_cast<std::size_t> (fftN / 2)));
        }

        // ---- Smooth RMS with a one-pole IIR ------------------------------
        {
            const float alpha = 0.15f;
            for (std::size_t f = 1; f < numFrames; ++f)
                rmsVec[f] = alpha * rmsVec[f] + (1.0f - alpha) * rmsVec[f - 1];
        }

        // ---- Windowed centroid stability ---------------------------------
        const int stabilityWinFrames = std::max (4, static_cast<int> (
            std::round (params_.stabilityWindowSec * sr
                       / static_cast<double> (hop))));

        // Reference scale: max centroid std-dev across the whole file.
        // RingVariance gives O(1) per frame instead of O(window) with erase().
        float maxStdDev = 1.0f;
        {
            RingVariance rv (stabilityWinFrames);
            for (std::size_t f = 0; f < numFrames; ++f)
                maxStdDev = std::max (maxStdDev, rv.push (centVec[f]));
        }

        std::vector<float> stabilityVec (numFrames);
        {
            const float effThreshold = params_.stabilityThreshold
                                     / std::max (0.1f, params_.sensitivity);
            RingVariance rv (stabilityWinFrames);
            for (std::size_t f = 0; f < numFrames; ++f)
            {
                const float sd = rv.push (centVec[f]);
                stabilityVec[f] = 1.0f - std::min (1.0f, sd / maxStdDev / effThreshold);
            }
        }

        // ---- Detect stable-region starts ---------------------------------
        const int minSpacingFrames = std::max (1, static_cast<int> (
            std::round (params_.minSpacingMs * 1e-3 * sr
                       / static_cast<double> (hop))));

        std::vector<Transient> results;
        const float effStabThreshold = params_.stabilityThreshold
                                     / std::max (0.1f, params_.sensitivity);

        bool prevStable = false;
        int  lastFrame  = -minSpacingFrames;

        for (std::size_t f = 1; f < numFrames; ++f)
        {
            const bool active = rmsVec[f]      >= params_.rmsFloor;
            const bool stable = stabilityVec[f] >= effStabThreshold;
            const bool transition = active && stable && !prevStable;

            if (transition
                && (static_cast<int> (f) - lastFrame) >= minSpacingFrames)
            {
                Transient t;
                t.rawSampleIndex = static_cast<std::int64_t> (f)
                                 * static_cast<std::int64_t> (hop);
                t.sampleIndex    = t.rawSampleIndex;
                t.fluxValue      = stabilityVec[f];
                t.confidence     = stabilityVec[f];
                results.push_back (t);
                lastFrame = static_cast<int> (f);
            }
            prevStable = active && stable;
        }

        // ---- Zero-crossing snap ------------------------------------------
        const std::int64_t snapRadius = static_cast<std::int64_t> (
            std::llround (0.005 * sr));   // 5 ms

        for (auto& t : results)
        {
            t.sampleIndex = snapToZeroCrossing (
                std::span<const float> (mono.data(), mono.size()),
                t.rawSampleIndex, snapRadius);
            t.timeSeconds = static_cast<double> (t.sampleIndex) / sr;
        }

        return results;
    }
} // namespace switchblade::analysis
