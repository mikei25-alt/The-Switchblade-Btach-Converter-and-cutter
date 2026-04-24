#pragma once

#include "Analysis/AudioFile.h"
#include "Analysis/TransientDetector.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <memory>

namespace switchblade::ui
{
    //==========================================================================
    //  GridVoiceBank — lightweight polyphonic AudioSource that mixes up to
    //  kMaxVoices slices from a shared AudioFile.
    //
    //  Thread Safety Model:
    //  -------------------
    //  - trigger() called from UI thread (MessageManager) via mouse/keyboard
    //  - getNextAudioBlock() called from audio device thread (real-time)
    //  - lock_ (CriticalSection) guards voice allocation and playback state
    //  - Audio thread uses ScopedTryLock — if contended, skips block (rare)
    //    At 44.1 kHz with 64-sample blocks, each audio frame is ~1.45 ms.
    //    Contention requires UI to spam triggers faster than audio frames.
    //
    //  TODO: Replace with lock-free SPSC queue for production (zero contention)
    //==========================================================================
    class GridVoiceBank final : public juce::AudioSource
    {
    public:
        static constexpr int kMaxVoices = 8;
        using AudioFilePtr = std::shared_ptr<const switchblade::analysis::AudioFile>;

        GridVoiceBank() = default;

        void trigger (AudioFilePtr file, juce::int64 start, juce::int64 end);
        void stopAll() noexcept;

        //----- AudioSource ---------------------------------------------------
        void prepareToPlay (int blockSize, double sampleRate) override;
        void releaseResources() override;
        void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;

    private:
        struct Voice
        {
            AudioFilePtr file;
            juce::int64  start       { 0 };
            juce::int64  end         { 0 };
            juce::int64  playhead    { 0 };
            bool         active      { false };
        };

        std::array<Voice, kMaxVoices> voices_;
        juce::CriticalSection          lock_;
        double                         deviceSampleRate_ { 44100.0 };
    };

    //==========================================================================
    //  PreviewGrid — 4x4 pad grid. Maps the first 16 transients of the
    //  selected SampleCard to pads. Keys:
    //        Row 1: 1 2 3 4
    //        Row 2: Q W E R
    //        Row 3: A S D F
    //        Row 4: Z X C V
    //==========================================================================
    class PreviewGrid final : public juce::Component,
                              public juce::KeyListener,
                              private juce::Timer
    {
    public:
        using AudioFilePtr = std::shared_ptr<const switchblade::analysis::AudioFile>;

        PreviewGrid();
        ~PreviewGrid() override;

        /** Bind a source file + detected transients; slices the first 16
            onset-to-onset regions into pads. */
        void setSource (AudioFilePtr file,
                        std::vector<switchblade::analysis::Transient> transients);

        /** Clear all pads (e.g. when no card is selected). */
        void clear() noexcept;

        /** Trigger a one-shot slice directly (e.g. from ResultsVault). */
        void playSlice (AudioFilePtr file, juce::int64 start, juce::int64 end)
        {
            voiceBank_.trigger (std::move (file), start, end);
        }

        /** Public audio source to wire into a device manager. */
        [[nodiscard]] juce::AudioSource& getAudioSource() noexcept { return voiceBank_; }

        //----- Component -----------------------------------------------------
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

        //----- KeyListener ---------------------------------------------------
        bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    private:
        struct Pad
        {
            juce::int64 start { -1 };
            juce::int64 end   { -1 };
            juce::String label;
            float        flash { 0.0f };  // 1.0 on trigger, decays to 0
        };

        static constexpr int kRows = 4;
        static constexpr int kCols = 4;
        static constexpr int kNumPads = kRows * kCols;

        static constexpr std::array<char, kNumPads> kKeyMap {
            '1','2','3','4',
            'Q','W','E','R',
            'A','S','D','F',
            'Z','X','C','V'
        };

        [[nodiscard]] juce::Rectangle<int> padBounds (int index) const;
        [[nodiscard]] int padAt (juce::Point<int> p) const;
        void triggerPad (int index);

        //----- Timer ---------------------------------------------------------
        void timerCallback() override;

        std::array<Pad, kNumPads>     pads_;
        AudioFilePtr                  file_;
        GridVoiceBank                 voiceBank_;

        JUCE_LEAK_DETECTOR (PreviewGrid)
    };
} // namespace switchblade::ui
