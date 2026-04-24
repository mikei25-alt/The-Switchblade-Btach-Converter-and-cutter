#pragma once

#include "Analysis/AudioFile.h"
#include "Analysis/AnalysisEngine.h"  // SourceClass

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <memory>

namespace switchblade::ui
{
    //==========================================================================
    //  ResultTile
    //
    //  Compact chrome tile (kTileH px tall) representing one extracted slice.
    //  Renders a mini NeonCyan waveform, index, classification tag, and duration.
    //  Entry animation: white-hot cooling glow driven by VBlankAttachment.
    //  Single click fires onPlay; double-click fires onSelected.
    //==========================================================================
    class ResultTile final : public juce::Component
    {
    public:
        using AudioFilePtr = std::shared_ptr<const switchblade::analysis::AudioFile>;

        static constexpr int kTileH = 68;

        ResultTile (juce::AudioFormatManager& fmt,
                    juce::AudioThumbnailCache& cache,
                    AudioFilePtr file,
                    juce::int64 startSample,
                    juce::int64 endSample,
                    switchblade::analysis::SourceClass classification,
                    int sliceIndex);
        ~ResultTile() override;

        ResultTile (const ResultTile&) = delete;
        ResultTile& operator= (const ResultTile&) = delete;

        /** Kick off the white-hot → NeonCyan cooling glow. Call after addAndMakeVisible. */
        void triggerEntryGlow() noexcept;

        [[nodiscard]] AudioFilePtr  file()        const noexcept { return file_; }
        [[nodiscard]] juce::int64   startSample() const noexcept { return startSample_; }
        [[nodiscard]] juce::int64   endSample()   const noexcept { return endSample_; }

        std::function<void (AudioFilePtr, juce::int64, juce::int64)> onPlay;
        std::function<void (AudioFilePtr, juce::int64, juce::int64)> onSelected;

        //----- Component -------------------------------------------------------
        void paint     (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        void buildThumbnail();
        void onVBlank();
        [[nodiscard]] juce::String durationStr()  const noexcept;
        [[nodiscard]] const char*  classTag()     const noexcept;
        [[nodiscard]] juce::Colour classColour()  const noexcept;

        juce::AudioFormatManager&             fmt_;
        juce::AudioThumbnailCache&            cache_;
        std::unique_ptr<juce::AudioThumbnail> thumbnail_;

        AudioFilePtr                         file_;
        juce::int64                          startSample_;
        juce::int64                          endSample_;
        switchblade::analysis::SourceClass   classification_;
        int                                  sliceIndex_;

        float entryGlow_ { 0.0f };   // 1 = white-hot, decays to 0 over ~1.2s

        juce::VBlankAttachment vblank_;

        JUCE_LEAK_DETECTOR (ResultTile)
    };
} // namespace switchblade::ui
