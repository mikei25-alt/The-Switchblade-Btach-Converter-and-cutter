// =============================================================================
//  slice_fades_test.cpp
//
//  Verifies the shared anti-click fade profile applied at both the export
//  pipeline (renderSliceToWav) and the in-app preview (GridVoiceBank). If
//  these tests pass, what the user hears in the Vault is identical to what
//  the DAW receives on import.
// =============================================================================

#include <gtest/gtest.h>

#include "Analysis/SliceFades.h"

using switchblade::analysis::FadeProfile;
using switchblade::analysis::sliceFades;
using switchblade::analysis::kFadeInMs;
using switchblade::analysis::kFadeOutMs;

namespace
{
    constexpr double kSR = 44100.0;

    // Helper: ms -> samples at the test sample rate.
    inline int ms (double v) { return static_cast<int> (v * 0.001 * kSR + 0.5); }
}

TEST(SliceFades, ConstantsMatchSpec)
{
    EXPECT_DOUBLE_EQ(kFadeInMs,  5.0);
    EXPECT_DOUBLE_EQ(kFadeOutMs, 30.0);
}

TEST(SliceFades, LongSliceGetsFullWantedFades)
{
    // 1 s @ 44.1 kHz — way longer than 4*(fadeIn + fadeOut), so no cap kicks in.
    const auto f = sliceFades(ms(1000.0), kSR);
    EXPECT_EQ(f.fadeInSamples,  ms(kFadeInMs));    // 220 samples
    EXPECT_EQ(f.fadeOutSamples, ms(kFadeOutMs));   // 1323 samples
}

TEST(SliceFades, CapKicksInAtSliceQuarter)
{
    // 80 ms slice. fadeIn wants 5 ms (220), fadeOut wants 30 ms (1323).
    // Cap = sliceLen / 4 = 80 ms / 4 = 20 ms (882 samples).
    // fadeIn stays at 220 (below cap), fadeOut clamps to 882.
    const int len = ms(80.0);
    const auto f = sliceFades(len, kSR);
    EXPECT_EQ(f.fadeInSamples,  ms(5.0));
    EXPECT_EQ(f.fadeOutSamples, len / 4);
}

TEST(SliceFades, VeryShortSliceCapsBoth)
{
    // 10 ms slice — cap = 110 samples. fadeIn wanted 220, fadeOut wanted 1323.
    // Both clamp to 110.
    const int len = ms(10.0);
    const auto f = sliceFades(len, kSR);
    EXPECT_EQ(f.fadeInSamples,  len / 4);
    EXPECT_EQ(f.fadeOutSamples, len / 4);
}

TEST(SliceFades, FadesNeverExceedSliceLength)
{
    // Across many lengths: fadeIn + fadeOut must always fit inside the slice
    // (no overlap into the middle). With cap = len/4, total is at most len/2.
    for (int len = 1; len <= 200000; len = (len < 100 ? len + 1 : len * 2))
    {
        const auto f = sliceFades(len, kSR);
        EXPECT_GE(f.fadeInSamples,  0);
        EXPECT_GE(f.fadeOutSamples, 0);
        EXPECT_LE(f.fadeInSamples  + f.fadeOutSamples, len);
    }
}

TEST(SliceFades, ZeroOrNegativeInputsReturnZeroProfile)
{
    EXPECT_EQ(sliceFades(0,    kSR).fadeInSamples,  0);
    EXPECT_EQ(sliceFades(0,    kSR).fadeOutSamples, 0);
    EXPECT_EQ(sliceFades(-100, kSR).fadeInSamples,  0);
    EXPECT_EQ(sliceFades(-100, kSR).fadeOutSamples, 0);
    EXPECT_EQ(sliceFades(1000,  0.0).fadeInSamples,  0);
    EXPECT_EQ(sliceFades(1000, -1.0).fadeOutSamples, 0);
}

TEST(SliceFades, ScalesWithSampleRate)
{
    // At 96 kHz the fade lengths in samples should grow proportionally.
    const auto f44 = sliceFades(ms(1000.0), 44100.0);
    const int  ms5_96k  = static_cast<int> (kFadeInMs  * 0.001 * 96000.0 + 0.5);
    const int  ms30_96k = static_cast<int> (kFadeOutMs * 0.001 * 96000.0 + 0.5);
    const auto f96 = sliceFades(96000 /* 1 s @ 96k */, 96000.0);
    EXPECT_EQ(f96.fadeInSamples,  ms5_96k);
    EXPECT_EQ(f96.fadeOutSamples, ms30_96k);
    EXPECT_GT(f96.fadeInSamples,  f44.fadeInSamples);
    EXPECT_GT(f96.fadeOutSamples, f44.fadeOutSamples);
}
