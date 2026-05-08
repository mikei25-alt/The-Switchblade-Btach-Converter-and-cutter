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
    }

    std::vector<float> SpectralFlux::process (std::span<const float> mono)
    {
        const int N      = fftSize();
        const int hop    = cfg_.hopSize;
        const int nBins  = N / 2;
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
            const std::size_t start = f * static_cast<std::size_t> (hop);

            // Copy frame into real half, zero imag half.
            // std::copy instead of fill-then-loop halves the zero-write count.
            std::copy (mono.data() + start,
                       mono.data() + start + static_cast<std::size_t> (N),
                       fftScratch_.data());
            std::fill (fftScratch_.data() + N,
                       fftScratch_.data() + 2 * N, 0.0f);

            window_.multiplyWithWindowingTable (fftScratch_.data(),
                                                static_cast<std::size_t> (N));
            fft_.performFrequencyOnlyForwardTransform (fftScratch_.data());

            // Branchless half-wave rectified diff + in-place prevMag update.
            // Eliminates the curMag_ intermediate copy and std::swap entirely.
            // std::max(0,x) auto-vectorises to MAXPS; the branch version doesn't.
            float sf = 0.0f;
            for (int k = 0; k < nBins; ++k)
            {
                const float cur  = fftScratch_[static_cast<std::size_t> (k)];
                sf += std::max (0.0f, cur - prevMag_[static_cast<std::size_t> (k)]);
                prevMag_[static_cast<std::size_t> (k)] = cur;
            }

            novelty.push_back (sf / static_cast<float> (nBins));
        }

        for (auto& v : novelty)
            v = std::sqrt (std::max (0.0f, v));

        return novelty;
    }
} // namespace switchblade::analysis
