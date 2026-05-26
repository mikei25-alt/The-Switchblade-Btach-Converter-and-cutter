// =============================================================================
//  cents_suffix_test.cpp
//
//  Validates PitchDetector::noteNameWithCentsFromHz format and runs the live
//  pitch path on a real cello sample to confirm what filename suffix the
//  export pipeline would emit.
//
//  The live-file test is opt-in by absolute path; if the file isn't present
//  it is SKIPPED (not failed) so the suite stays portable.
// =============================================================================

#include <gtest/gtest.h>

#include "Analysis/AudioFile.h"
#include "Analysis/PitchDetector.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <filesystem>
#include <string>

using switchblade::analysis::PitchDetector;
using switchblade::analysis::detectSlicePitchHz;
using switchblade::analysis::loadAudioFile;

// ---------------------------------------------------------------------------
//  Synthetic format checks
// ---------------------------------------------------------------------------

TEST(CentsSuffix, InTuneOmitsSuffix)
{
    EXPECT_EQ(PitchDetector::noteNameWithCentsFromHz(440.0f), "A4");
    EXPECT_EQ(PitchDetector::noteNameWithCentsFromHz(261.6256f), "C4");
}

TEST(CentsSuffix, SharpSignedTwoDigit)
{
    const float a4Plus12 = 440.0f * std::pow(2.0f, 12.0f / 1200.0f);
    EXPECT_EQ(PitchDetector::noteNameWithCentsFromHz(a4Plus12), "A4+12c");

    const float a4Plus08 = 440.0f * std::pow(2.0f, 8.0f / 1200.0f);
    EXPECT_EQ(PitchDetector::noteNameWithCentsFromHz(a4Plus08), "A4+08c");
}

TEST(CentsSuffix, FlatSignedTwoDigit)
{
    const float a4Minus08 = 440.0f * std::pow(2.0f, -8.0f / 1200.0f);
    EXPECT_EQ(PitchDetector::noteNameWithCentsFromHz(a4Minus08), "A4-08c");

    const float a4Minus42 = 440.0f * std::pow(2.0f, -42.0f / 1200.0f);
    EXPECT_EQ(PitchDetector::noteNameWithCentsFromHz(a4Minus42), "A4-42c");
}

TEST(CentsSuffix, NonPositiveReturnsQuestionMark)
{
    EXPECT_EQ(PitchDetector::noteNameWithCentsFromHz(0.0f),   "?");
    EXPECT_EQ(PitchDetector::noteNameWithCentsFromHz(-50.0f), "?");
}

TEST(CentsSuffix, RangeIsBoundedToHalfSemitone)
{
    for (float cents = -50.0f; cents <= 50.0f; cents += 1.0f)
    {
        const float hz = 440.0f * std::pow(2.0f, cents / 1200.0f);
        const int   c  = PitchDetector::centsOffsetFromHz(hz);
        EXPECT_GE(c, -50);
        EXPECT_LE(c,  50);
    }
}

// ---------------------------------------------------------------------------
//  Live-file smoke test
// ---------------------------------------------------------------------------

TEST(CentsSuffix, CelloA1SampleFromDisk)
{
    const std::filesystem::path celloPath =
        R"(D:\Drum Samples\MIke's samples\Rosie Cello Recording merged - Cello Low  Close_A1_016.wav)";

    if (! std::filesystem::exists(celloPath))
        GTEST_SKIP() << "Sample not present at " << celloPath.string();

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();

    auto file = loadAudioFile(fmt, celloPath);
    ASSERT_TRUE(file.has_value()) << "Failed to load: " << celloPath.string();

    const juce::int64 len = file->samples.getNumSamples();
    ASSERT_GT(len, 0);

    const auto hz = detectSlicePitchHz(*file, 0, len);

    if (! hz.has_value())
    {
        std::cout << "[cello] detectSlicePitchHz returned nullopt — "
                     "filename would have no note suffix.\n";
        GTEST_SKIP() << "No pitch detected (atonal path).";
    }

    const std::string note  = PitchDetector::noteNameFromHz(*hz);
    const int         cents = PitchDetector::centsOffsetFromHz(*hz);
    const std::string nwc   = PitchDetector::noteNameWithCentsFromHz(*hz);

    std::cout << "[cello] f0       = " << *hz   << " Hz\n"
              << "[cello] note     = " << note  << '\n'
              << "[cello] cents    = " << cents << '\n'
              << "[cello] suffix   = " << nwc   << '\n'
              << "[cello] filename would be: "
                 "RosieCello_..._" << nwc << "_NN.wav\n";

    EXPECT_FALSE(note.empty());
    EXPECT_NE(note, "?");
}
