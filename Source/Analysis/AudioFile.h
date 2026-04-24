#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <concepts>
#include <filesystem>
#include <span>

namespace switchblade::analysis
{
    //==========================================================================
    //  Concepts — make templated DSP operate on anything that quacks like an
    //  interleaved/planar audio source, without baking JUCE types into every
    //  algorithm.
    //==========================================================================
    template<typename T>
    concept FloatSample = std::floating_point<T>;

    template<typename B>
    concept AudioBufferLike = requires (B& b, int ch)
    {
        { b.getNumChannels() }                         -> std::convertible_to<int>;
        { b.getNumSamples() }                          -> std::convertible_to<int>;
        { b.getReadPointer (ch) }                      -> std::convertible_to<const float*>;
    };

    //==========================================================================
    //  AudioFile — owning container for a loaded file + its metadata.
    //  Immutable after construction; cheap to move, expensive to copy
    //  (copying is explicitly deleted to force intent).
    //==========================================================================
    struct AudioFile
    {
        std::filesystem::path        path;
        juce::AudioBuffer<float>     samples;
        double                       sampleRate { 0.0 };
        juce::int64                  originalLengthInSamples { 0 };

        AudioFile() = default;
        AudioFile (AudioFile&&) noexcept = default;
        AudioFile& operator= (AudioFile&&) noexcept = default;

        AudioFile (const AudioFile&) = delete;
        AudioFile& operator= (const AudioFile&) = delete;

        [[nodiscard]] bool isValid() const noexcept
        {
            return sampleRate > 0.0 && samples.getNumSamples() > 0;
        }

        [[nodiscard]] double durationSeconds() const noexcept
        {
            return sampleRate > 0.0
                   ? static_cast<double> (samples.getNumSamples()) / sampleRate
                   : 0.0;
        }
    };

    //==========================================================================
    //  Load a file from disk. Returns nullopt on any I/O / format error.
    //  The supplied AudioFormatManager must already have registered the
    //  formats you care about (WAV/AIFF by default).
    //==========================================================================
    [[nodiscard]] std::optional<AudioFile> loadAudioFile (
        juce::AudioFormatManager& formatManager,
        const std::filesystem::path& path);

    //==========================================================================
    //  Mixdown helper — returns a mono view of a multi-channel buffer by
    //  averaging channels into the supplied scratch vector. Zero-alloc if
    //  scratch is pre-sized correctly.
    //==========================================================================
    void mixToMono (const juce::AudioBuffer<float>& src,
                    std::vector<float>& monoOut);

    //==========================================================================
    //  Inline implementations (short enough to stay header-only)
    //==========================================================================
    inline std::optional<AudioFile> loadAudioFile (juce::AudioFormatManager& fmt,
                                                   const std::filesystem::path& path)
    {
        auto juceFile = juce::File (juce::String (path.string()));
        std::unique_ptr<juce::AudioFormatReader> reader { fmt.createReaderFor (juceFile) };
        if (reader == nullptr)
            return std::nullopt;

        AudioFile out;
        out.path = path;
        out.sampleRate = reader->sampleRate;
        out.originalLengthInSamples = static_cast<juce::int64> (reader->lengthInSamples);

        const auto numChannels = static_cast<int> (reader->numChannels);
        const auto numSamples  = static_cast<int> (reader->lengthInSamples);
        if (numChannels <= 0 || numSamples <= 0)
            return std::nullopt;

        out.samples.setSize (numChannels, numSamples, false, true, false);
        if (! reader->read (&out.samples, 0, numSamples, 0, true, true))
            return std::nullopt;

        return out;
    }

    inline void mixToMono (const juce::AudioBuffer<float>& src,
                           std::vector<float>& mono)
    {
        const auto numCh = src.getNumChannels();
        const auto numS  = src.getNumSamples();
        mono.assign (static_cast<std::size_t> (numS), 0.0f);

        if (numCh <= 0 || numS <= 0)
            return;

        const float gain = 1.0f / static_cast<float> (numCh);
        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto* r = src.getReadPointer (ch);
            for (int i = 0; i < numS; ++i)
                mono[static_cast<std::size_t> (i)] += r[i] * gain;
        }
    }
} // namespace switchblade::analysis
