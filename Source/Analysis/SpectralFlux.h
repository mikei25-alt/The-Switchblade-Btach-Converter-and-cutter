#pragma once

#include <juce_dsp/juce_dsp.h>

#include <span>
#include <vector>

namespace switchblade::analysis
{
    //==========================================================================
    //  SpectralFlux — streaming spectral-flux novelty function.
    //
    //      SF(n) = sum_k H(|X(n,k)| - |X(n-1,k)|)
    //
    //  where H is the half-wave rectifier (max(0, .)) — we only count
    //  increases in spectral energy, which correspond to onsets.
    //
    //  Pipeline:  mono input -> Hann window -> real FFT -> magnitude ->
    //             per-frame HWR diff -> scalar novelty value per hop.
    //
    //  The computed novelty curve is what AdaptiveThreshold gates on.
    //==========================================================================
    class SpectralFlux
    {
    public:
        struct Config
        {
            int fftOrder { 11 };    // 2048
            int hopSize  { 512 };   // ~11.6 ms at 44.1 kHz
        };

        explicit SpectralFlux (Config cfg = {});

        /** Run across an entire mono buffer, returning the novelty curve.
            One output sample per hop. */
        [[nodiscard]] std::vector<float> process (std::span<const float> mono);

        [[nodiscard]] int fftSize()      const noexcept { return 1 << cfg_.fftOrder; }
        [[nodiscard]] int hopSize()      const noexcept { return cfg_.hopSize; }
        [[nodiscard]] const Config& config() const noexcept { return cfg_; }

    private:
        Config                      cfg_;
        juce::dsp::FFT              fft_;
        juce::dsp::WindowingFunction<float> window_;
        std::vector<float>          fftScratch_;     // 2 * fftSize
        std::vector<float>          prevMag_;        // fftSize/2 — updated in-place each hop
    };
} // namespace switchblade::analysis
