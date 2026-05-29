// =============================================================================
//  bwav_meta_test.cpp
//
//  Verifies the Broadcast-WAV (EBU R 98) bext chunk Switchblade attaches to
//  every exported slice. Two layers of coverage:
//    1. buildBwavMeta() populates the expected keys with the expected values.
//    2. JUCE's WavAudioFormat round-trips those keys to/from disk: write a
//       tiny WAV with the meta, re-read it, and confirm bext fields survive.
//
//  If either layer fails, slices the user drags into a DAW will lack the
//  source-file linkage and timeline offset that lets the DAW re-align them.
// =============================================================================

#include <gtest/gtest.h>

#include "Analysis/BwavMeta.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
    constexpr double kSR = 44100.0;
}

TEST(BwavMeta, KeysPopulated)
{
    const juce::Time t { 2026, 4 /*May, 0-indexed*/, 28, 14, 23, 7, 0, false /*UTC*/ };
    const auto m = switchblade::analysis::buildBwavMeta (
        std::filesystem::path { "C:/loops/BassLoop.wav" },
        "Bass_E1+12c_03",
        44100,
        t);

    EXPECT_EQ (m[juce::WavAudioFormat::bwavDescription],
               "Switchblade slice: Bass_E1+12c_03 from BassLoop.wav");
    EXPECT_EQ (m[juce::WavAudioFormat::bwavOriginator],    "Switchblade");
    EXPECT_EQ (m[juce::WavAudioFormat::bwavOriginatorRef], "BassLoop.wav");
    EXPECT_EQ (m[juce::WavAudioFormat::bwavOriginationDate], "2026-05-28");
    EXPECT_EQ (m[juce::WavAudioFormat::bwavOriginationTime], "14:23:07");
    EXPECT_EQ (m[juce::WavAudioFormat::bwavTimeReference],   "44100");
}

TEST(BwavMeta, SourceNameStripsParentPath)
{
    const auto m = switchblade::analysis::buildBwavMeta (
        std::filesystem::path { "/some/deep/folder/structure/Kick.wav" },
        "Kick_drum_01",
        0,
        juce::Time { 2026, 0, 1, 0, 0, 0, 0, false /*UTC*/ });

    EXPECT_EQ (m[juce::WavAudioFormat::bwavOriginatorRef], "Kick.wav");
    EXPECT_TRUE (m[juce::WavAudioFormat::bwavDescription]
                     .endsWith ("from Kick.wav"));
}

TEST(BwavMeta, TimeReferenceIsDecimalSampleOffset)
{
    const auto m = switchblade::analysis::buildBwavMeta (
        std::filesystem::path { "x.wav" }, "x_01", 123456789LL,
        juce::Time { 2026, 0, 1, 0, 0, 0, 0, false /*UTC*/ });
    EXPECT_EQ (m[juce::WavAudioFormat::bwavTimeReference], "123456789");
}

TEST(BwavMeta, DescriptionCapped256Bytes)
{
    const juce::String longStem (juce::String::repeatedString ("X", 400));
    const auto m = switchblade::analysis::buildBwavMeta (
        std::filesystem::path { "src.wav" }, longStem, 0,
        juce::Time { 2026, 0, 1, 0, 0, 0, 0, false });

    const auto desc = m[juce::WavAudioFormat::bwavDescription];
    EXPECT_LE (static_cast<int> (desc.getNumBytesAsUTF8()), 256);
    EXPECT_TRUE (desc.startsWith ("Switchblade slice: XXX"));
}

TEST(BwavMeta, NonAsciiSourceNamePreserved)
{
    // U+00E4 (ä) is outside ASCII. The path MUST enter the function as a
    // UTF-8-native filesystem::path; building it from std::string on Windows
    // would re-narrow through the ANSI codepage and pre-corrupt the test.
    const std::u8string u8name { u8"Käfer.wav" };
    const std::filesystem::path src { u8name };
    const auto m = switchblade::analysis::buildBwavMeta (
        src, "stem", 0,
        juce::Time { 2026, 0, 1, 0, 0, 0, 0, false });

    const auto ref  = m[juce::WavAudioFormat::bwavOriginatorRef];
    const auto desc = m[juce::WavAudioFormat::bwavDescription];
    EXPECT_TRUE (ref.containsChar (juce::juce_wchar (0x00E4)));
    EXPECT_TRUE (desc.containsChar (juce::juce_wchar (0x00E4)));
}

TEST(BwavMeta, JuceWavRoundTripsBextChunk)
{
    // Write a tiny WAV with the bwav metadata, re-read it, and confirm the
    // bext fields survive a JUCE writer -> reader round trip.
    const auto meta = switchblade::analysis::buildBwavMeta (
        std::filesystem::path { "BassLoop.wav" }, "Bass_E1_01", 44100,
        juce::Time { 2026, 4, 28, 14, 23, 7, 0, false /*UTC*/ });

    juce::AudioBuffer<float> buf (1, 1024);
    buf.clear();

    const auto tmp = juce::File::createTempFile (".wav");

    {
        juce::WavAudioFormat wav;
        auto* os = tmp.createOutputStream().release();
        ASSERT_NE (os, nullptr);
        const auto writer = std::unique_ptr<juce::AudioFormatWriter> (
            wav.createWriterFor (os, kSR, 1, 24, meta, 0));
        ASSERT_NE (writer, nullptr);
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    juce::WavAudioFormat wav;
    const auto reader = std::unique_ptr<juce::AudioFormatReader> (
        wav.createReaderFor (tmp.createInputStream().release(), true));
    ASSERT_NE (reader, nullptr);

    const auto& md = reader->metadataValues;
    EXPECT_EQ (md[juce::WavAudioFormat::bwavOriginator],    "Switchblade");
    EXPECT_EQ (md[juce::WavAudioFormat::bwavOriginatorRef], "BassLoop.wav");
    EXPECT_TRUE (md[juce::WavAudioFormat::bwavDescription]
                     .startsWith ("Switchblade slice: Bass_E1_01"));
    EXPECT_EQ (md[juce::WavAudioFormat::bwavOriginationDate], "2026-05-28");
    EXPECT_EQ (md[juce::WavAudioFormat::bwavTimeReference],   "44100");

    tmp.deleteFile();
}
