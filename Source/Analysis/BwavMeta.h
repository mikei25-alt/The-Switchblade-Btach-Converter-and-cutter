#pragma once

// =============================================================================
//  BwavMeta — Broadcast-WAV (EBU R 98) bext chunk key population.
//
//  buildBwavMeta() returns a juce::StringPairArray with the bwav* keys that
//  juce::WavAudioFormat consumes to write a bext chunk into the output file.
//
//  Field conventions (chosen for portability + reproducibility):
//    Description     "Switchblade slice: <outStem> from <sourceName>",
//                    capped at 256 bytes per EBU R 98 §2.3.
//    Originator      "Switchblade"
//    OriginatorRef   <sourceName>
//    OriginationDate YYYY-MM-DD   (UTC, EBU R 98 §2.4)
//    OriginationTime HH:MM:SS     (UTC, EBU R 98 §2.5)
//    TimeReference   <startSample>  — sample offset within the source file;
//                                     lets a DAW re-align all slices on a
//                                     single timeline by reading this field.
//
//  Source filenames are converted from std::filesystem::path via u8string so
//  non-ASCII names survive the trip into the bext chunk (the previous
//  path.string() narrowing through the Windows ANSI codepage corrupted
//  characters outside the active codepage).
// =============================================================================

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <ctime>
#include <filesystem>

namespace switchblade::analysis
{
    namespace bwav_detail
    {
        [[nodiscard]] inline juce::String pathToUtf8 (
            const std::filesystem::path& p)
        {
            const auto u8 = p.u8string();
            return juce::String::fromUTF8 (
                reinterpret_cast<const char*> (u8.data()),
                static_cast<int> (u8.size()));
        }

        // Format a juce::Time as UTC strftime — bypasses local-time formatting
        // so two collaborators in different timezones get the same bext date.
        [[nodiscard]] inline juce::String formatUtc (
            juce::Time t, const char* fmt)
        {
            const std::time_t secs =
                static_cast<std::time_t> (t.toMilliseconds() / 1000);
            std::tm tm {};
           #if JUCE_WINDOWS
            ::gmtime_s (&tm, &secs);
           #else
            ::gmtime_r (&secs, &tm);
           #endif
            char buf[32] {};
            std::strftime (buf, sizeof (buf), fmt, &tm);
            return juce::String::fromUTF8 (buf);
        }

        // EBU R 98 §2.3 — Description is a fixed 256-byte ASCII field. Truncate
        // (in code-unit terms) so JUCE's writer doesn't silently spill into
        // adjacent bext fields. We round to a UTF-8 code-point boundary so the
        // truncated string stays syntactically valid even if a reader chooses
        // to interpret the field as UTF-8 rather than strict ASCII.
        inline constexpr int kMaxDescriptionBytes = 256;

        [[nodiscard]] inline juce::String capDescription (juce::String s)
        {
            if (s.getNumBytesAsUTF8() <= static_cast<std::size_t> (kMaxDescriptionBytes))
                return s;

            const auto utf8     = s.toRawUTF8();
            int        cutBytes = kMaxDescriptionBytes;
            // Walk back to the start of a UTF-8 code-point (continuation bytes
            // have the high bits 10xxxxxx — never cut between leader+continuation).
            while (cutBytes > 0
                   && (static_cast<unsigned char> (utf8[cutBytes]) & 0xC0) == 0x80)
                --cutBytes;
            return juce::String::fromUTF8 (utf8, cutBytes);
        }
    }

    [[nodiscard]] inline juce::StringPairArray buildBwavMeta (
        const std::filesystem::path& sourcePath,
        const juce::String&          outStem,
        juce::int64                  startSample,
        juce::Time                   timestamp = juce::Time::getCurrentTime())
    {
        using namespace bwav_detail;

        juce::StringPairArray m;
        const juce::String sourceName = pathToUtf8 (sourcePath.filename());

        m.set (juce::WavAudioFormat::bwavDescription,
               capDescription ("Switchblade slice: " + outStem
                               + " from " + sourceName));
        m.set (juce::WavAudioFormat::bwavOriginator,       "Switchblade");
        m.set (juce::WavAudioFormat::bwavOriginatorRef,    sourceName);
        m.set (juce::WavAudioFormat::bwavOriginationDate,  formatUtc (timestamp, "%Y-%m-%d"));
        m.set (juce::WavAudioFormat::bwavOriginationTime,  formatUtc (timestamp, "%H:%M:%S"));
        m.set (juce::WavAudioFormat::bwavTimeReference,    juce::String (startSample));
        return m;
    }
}
