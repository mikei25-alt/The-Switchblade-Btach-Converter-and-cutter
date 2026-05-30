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

// =============================================================================
//  SweepReport — exhaustive spectrum walk. Not a pass/fail check (gated by a
//  loose tolerance so it never fails), but a stdout table that shows the
//  engine's accuracy at every musically-interesting frequency. Run with:
//      SwitchbladeTests.exe --gtest_filter=PitchResolution.SweepReport
// =============================================================================
TEST(PitchResolution, SweepReport)
{
    struct Tone { float hz; const char* expectedNote; const char* tag; };
    const std::vector<Tone> sweep {
        // sub-bass region — would all collapse to bin 1 in a 1024-pt FFT
        { 30.87f, "B0",   "low B0"        },
        { 38.89f, "D#1",  "D#1"           },
        { 41.20f, "E1",   "E1 (low bass)" },
        { 43.65f, "F1",   "F1 (E1+1)"     },
        { 49.00f, "G1",   "G1"            },
        { 55.00f, "A1",   "A1"            },
        // bass
        { 65.41f, "C2",   "C2"            },
        { 82.41f, "E2",   "E2 (open low E)"},
        { 110.0f, "A2",   "A2"            },
        { 116.54f,"A#2",  "A#2 (A2+1)"    },
        { 130.81f,"C3",   "C3"            },
        // mid
        { 220.0f, "A3",   "A3"            },
        { 261.63f,"C4",   "C4 (middle C)" },
        { 329.63f,"E4",   "E4"            },
        { 440.0f, "A4",   "A4 (tuning)"   },
        { 466.16f,"A#4",  "A#4 (A4+1)"    },
        // high
        { 523.25f,"C5",   "C5"            },
        { 659.26f,"E5",   "E5"            },
        { 880.0f, "A5",   "A5"            },
        { 1046.5f,"C6",   "C6"            },
        // detuned — the real-world case
        { 441.0f,  "A4",  "A4 +04c"       },
        { 443.06f, "A4",  "A4 +12c"       },
        { 437.0f,  "A4",  "A4 -12c"       },
        { 446.16f, "A4",  "A4 +24c (qrtr)"},
    };

    std::printf ("\n");
    std::printf ("   target Hz    detected Hz    error Hz   note    cents  label\n");
    std::printf ("   ---------    -----------    --------   ----    -----  --------\n");

    using PD = switchblade::analysis::PitchDetector;
    int pass = 0;
    for (const auto& t : sweep)
    {
        const auto buf = sine (t.hz, 22050);
        const auto hz  = switchblade::analysis::detectSlicePitchHz (buf, 0, 22050);
        if (! hz.has_value())
        {
            std::printf ("   %8.2f     %s\n", t.hz, "(no detection)");
            continue;
        }
        const auto note  = PD::noteNameFromHz (*hz);
        const auto cents = PD::centsOffsetFromHz (*hz);
        const auto label = PD::noteNameWithCentsFromHz (*hz);
        const float err  = *hz - t.hz;
        const bool  ok   = note == t.expectedNote;
        if (ok) ++pass;
        std::printf ("   %8.2f      %8.3f    %+7.3f   %-4s   %+4d   %-10s  %s\n",
                     t.hz, *hz, err, note.c_str(), cents,
                     label.c_str(), ok ? "OK" : "MISS");
    }
    std::printf ("\n   %d / %zu notes matched expectation\n\n",
                 pass, sweep.size());
    EXPECT_EQ (pass, static_cast<int> (sweep.size()));
}
