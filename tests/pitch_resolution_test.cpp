// =============================================================================
//  pitch_resolution_test.cpp
//
//  Synthetic-tone proof that the YIN pitch detector resolves adjacent
//  semitones AND sub-semitone cents offsets in the low/mid range where an
//  FFT-with-1024-bin detector would fuse them into a single bucket.
//
//  Test cases are picked from the worst case for an FFT-based picker:
//    E1 = 41.20 Hz vs F1 = 43.65 Hz    (low-bass adjacent semitones)
//    A2 = 110.00 Hz vs A#2 = 116.54 Hz (mid-bass adjacent semitones)
//    A4 = 440.00 Hz vs A4 +12c = 443.06 Hz (sub-semitone discrimination)
//
//  At fs = 44.1 kHz and N = 1024 the FFT bin width is ~43 Hz — E1/F1 land
//  in the same bin. Our detector runs YIN in the time domain with
//  parabolic interpolation, so the τ values for those two notes differ
//  by ~60 samples and resolve cleanly.
// =============================================================================

#include <gtest/gtest.h>

#include "Analysis/AudioFile.h"
#include "Analysis/PitchDetector.h"

#include <cmath>
#include <numbers>
#include <vector>

namespace
{
    constexpr double kSR = 44100.0;

    // Build an AudioFile holding a pure sine of N samples at hz.
    [[nodiscard]] switchblade::analysis::AudioFile sine (float hz, int numSamples)
    {
        switchblade::analysis::AudioFile f;
        f.sampleRate              = kSR;
        f.originalLengthInSamples = numSamples;
        f.samples.setSize (1, numSamples);
        f.samples.clear();
        auto* w = f.samples.getWritePointer (0);
        const double k = 2.0 * std::numbers::pi * static_cast<double> (hz) / kSR;
        for (int i = 0; i < numSamples; ++i)
            w[i] = 0.5f * static_cast<float> (std::sin (k * i));
        return f;
    }
}

TEST(PitchResolution, ResolvesLowBassSemitonesE1vsF1)
{
    // 0.5 s of audio @ 44.1 kHz = 22050 samples
    const auto e1 = sine (41.20f, 22050);
    const auto f1 = sine (43.65f, 22050);

    const auto eHz = switchblade::analysis::detectSlicePitchHz (e1, 0, 22050);
    const auto fHz = switchblade::analysis::detectSlicePitchHz (f1, 0, 22050);

    ASSERT_TRUE (eHz.has_value());
    ASSERT_TRUE (fHz.has_value());

    EXPECT_NEAR (*eHz, 41.20f, 0.5f);
    EXPECT_NEAR (*fHz, 43.65f, 0.5f);

    using PD = switchblade::analysis::PitchDetector;
    EXPECT_EQ (PD::noteNameFromHz (*eHz), "E1");
    EXPECT_EQ (PD::noteNameFromHz (*fHz), "F1");
}

TEST(PitchResolution, ResolvesMidBassSemitonesA2vsBflat2)
{
    const auto a   = sine (110.00f, 22050);
    const auto bb  = sine (116.54f, 22050);

    const auto aHz  = switchblade::analysis::detectSlicePitchHz (a,  0, 22050);
    const auto bbHz = switchblade::analysis::detectSlicePitchHz (bb, 0, 22050);

    ASSERT_TRUE (aHz.has_value());
    ASSERT_TRUE (bbHz.has_value());

    using PD = switchblade::analysis::PitchDetector;
    EXPECT_EQ (PD::noteNameFromHz (*aHz),  "A2");
    EXPECT_EQ (PD::noteNameFromHz (*bbHz), "A#2");
}

TEST(PitchResolution, DetectsSubSemitoneCentsOffset)
{
    // A4 vs A4 +12 cents = 440 * 2^(12/1200) ≈ 443.06 Hz
    const float a4Plus12 = 440.0f * std::pow (2.0f, 12.0f / 1200.0f);

    const auto inTune = sine (440.0f,   22050);
    const auto sharp  = sine (a4Plus12, 22050);

    const auto a     = switchblade::analysis::detectSlicePitchHz (inTune, 0, 22050);
    const auto aPlus = switchblade::analysis::detectSlicePitchHz (sharp,  0, 22050);

    ASSERT_TRUE (a.has_value());
    ASSERT_TRUE (aPlus.has_value());

    using PD = switchblade::analysis::PitchDetector;
    // Both round to MIDI 69 ("A4"), but the cents suffix must differ —
    // proving the float precision survives all the way to the filename.
    EXPECT_EQ (PD::noteNameFromHz (*a),     "A4");
    EXPECT_EQ (PD::noteNameFromHz (*aPlus), "A4");

    const int cIn = PD::centsOffsetFromHz (*a);
    const int cUp = PD::centsOffsetFromHz (*aPlus);
    EXPECT_NEAR (cIn,  0,   3);
    EXPECT_NEAR (cUp, 12,   3);

    EXPECT_NE (PD::noteNameWithCentsFromHz (*a),
               PD::noteNameWithCentsFromHz (*aPlus));
}
