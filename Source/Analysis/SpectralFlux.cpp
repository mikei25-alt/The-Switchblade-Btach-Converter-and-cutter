#include "SpectralFlux.h"

#include <algorithm>
#include <cmath>

namespace switchblade::analysis
{
    SpectralFlux::SpectralFlux (Config cfg)
        : cfg_ (cfg),
          fft_ (cfg.fftOrder),
          window_ (static_cast<std::size_t> (1LL << cfg.fftOrder),
                   juce::dsp::WindowingFunction<float>::hann)
    {
        const int N = fftSize();
        fftScratch_.assign (static_cast<std::size_t> (2 * N), 0.0f);
        prevMag_.assign    (static_cast<std::size_t> (N / 2), 0.0f);
        curMag_.assign     (static_cast<std::size_t> (N / 2), 0.0f);
    }

    std::vector<float> SpectralFlux::process (std::span<const float> mono)
    {
        const int N   = fftSize();
        const int hop = cfg_.hopSize;
        const int nBins = N / 2;
        const auto totalSamples = static_cast<std::int64_t> (mono.size());
        if (totalSamples < N)
            return {};

        const auto numFrames = static_cast<std::size_t> (
            1 + (totalSamples - N) / hop);

        std::vector<float> novelty;
        novelty.reserve (numFrames);
        std::fill (prevMag_.begin(), prevMag_.end(), 0.0f);

        for (std::size_t f = 0; f < numFrames; ++f)
        {
            const std::int64_t start = static_cast<std::int64_t> (f) * hop;

            // Copy frame into scratch (real part), zero imag (upper half)
            std::fill (fftScratch_.begin(), fftScratch_.end(), 0.0f);
            for (int i = 0; i < N; ++i)
                fftScratch_[static_cast<std::size_t> (i)] =
                    mono[static_cast<std::size_t> (start + i)];

            window_.multiplyWithWindowingTable (fftScratch_.data(),
                                                static_cast<std::size_t> (N));
            fft_.performFrequencyOnlyForwardTransform (fftScratch_.data());

            // fftScratch_[0..N/2] now holds magnitudes
            for (int k = 0; k < nBins; ++k)
                curMag_[static_cast<std::size_t> (k)] =
                    fftScratch_[static_cast<std::size_t> (k)];

            // Half-wave rectified spectral difference
            float sf = 0.0f;
            for (int k = 0; k < nBins; ++k)
            {
                const float diff = curMag_[static_cast<std::size_t> (k)]
                                 - prevMag_[static_cast<std::size_t> (k)];
                if (diff > 0.0f)
                    sf += diff;
            }

            // Normalize by number of bins so values are dimensionally stable
            novelty.push_back (sf / static_cast<float> (nBins));
            std::swap (prevMag_, curMag_);
        }

        // Post-pass: compress dynamic range with a sqrt curve so adaptive
        // threshold behaves well on both quiet and loud material.
        for (auto& v : novelty)
            v = std::sqrt (std::max (0.0f, v));

        return novelty;
    }
} // namespace switchblade::analysis
