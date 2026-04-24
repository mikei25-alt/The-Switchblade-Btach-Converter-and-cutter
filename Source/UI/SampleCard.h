#pragma once

#include "Analysis/AudioFile.h"
#include "Analysis/TransientDetector.h"
#include "Analysis/AnalysisEngine.h"  // for SourceClass

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace switchblade::ui
{
    //==========================================================================
    //  SampleCard
    //
    //  One audio file as a visual "card" in the scrolling list. Responsibilities:
    //    * Own a shared_ptr to the loaded AudioFile (shared with PreviewGrid)
    //    * Render waveform (cyan bloom) + transient markers (neon gold)
    //    * Allow the user to nudge a marker horizontally (hit ±6px)
    //    * Lift on hover (Z-axis animation driven by VBlankAttachment)
    //    * Host an "Extract" TextButton styled by SwitchbladeLookAndFeel
    //
    //  The card does NOT drive analysis itself — transients are pushed in from
    //  MainContainer after the AnalysisEngine finishes a job.
    //==========================================================================
    class SampleCard final : public juce::Component
    {
    public:
        using AudioFilePtr = std::shared_ptr<const switchblade::analysis::AudioFile>;

        SampleCard (juce::AudioFormatManager& formatManager,
                    juce::AudioThumbnailCache& thumbnailCache);
        ~SampleCard() override;

        SampleCard (const SampleCard&) = delete;
        SampleCard& operator= (const SampleCard&) = delete;

        //----- Data setters (call from message thread) ------------------------
        void setFile (AudioFilePtr file);
        void setTransients (std::vector<switchblade::analysis::Transient> t);
        void setClassification (switchblade::analysis::SourceClass c);
        void setPitchHz (std::optional<float> hz) noexcept;
        void setPitchClarity (std::optional<float> clarity) noexcept;
        void setSelected (bool shouldBeSelected);

        /** Show "ANALYZING…" pulsing overlay when the background job is running. */
        void setLoading (bool isLoading) noexcept;

        /** Set the display path before audio is loaded (for pending-card flow). */
        void setDisplayPath (const std::filesystem::path& p);

        /** Kick off the "cooling glow" entry animation (call after setFile). */
        void triggerEntryGlow() noexcept;

        //----- Accessors ------------------------------------------------------
        [[nodiscard]] const AudioFilePtr& file() const noexcept { return file_; }
        [[nodiscard]] const std::vector<switchblade::analysis::Transient>&
                            transients() const noexcept { return transients_; }
        [[nodiscard]] switchblade::analysis::SourceClass
                            classification() const noexcept { return classification_; }
        [[nodiscard]] std::optional<float>
                            pitchHz() const noexcept { return pitchHz_; }
        [[nodiscard]] std::optional<float>
                            pitchClarity() const noexcept { return pitchClarity_; }
        [[nodiscard]] bool  isSelected() const noexcept { return selected_; }

        //----- Callbacks (set from owner) -------------------------------------
        std::function<void (int markerIndex, juce::int64 newSample)> onMarkerMoved;
        std::function<void()> onExtractClicked;
        std::function<void()> onSelected;

        //----- Component ------------------------------------------------------
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp   (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit  (const juce::MouseEvent&) override;

    private:
        void rebuildThumbnail();
        void rebuildMonoCache();
        [[nodiscard]] juce::Rectangle<int>   waveformBounds() const noexcept;
        [[nodiscard]] juce::Rectangle<int>   headerBounds()   const noexcept;
        [[nodiscard]] float xForSample (juce::int64 sample) const noexcept;
        [[nodiscard]] juce::int64 sampleForX (float x) const noexcept;
        [[nodiscard]] int hitTestMarker (juce::Point<float> localPoint) const noexcept;
        void animateLift();
        void paintHeader (juce::Graphics&, juce::Rectangle<int>) const;
        void paintWaveform (juce::Graphics&, juce::Rectangle<int>);
        void paintMarkers (juce::Graphics&, juce::Rectangle<int>) const;
        [[nodiscard]] juce::String classificationTag() const noexcept;
        [[nodiscard]] juce::Colour classificationColour() const noexcept;

        juce::AudioFormatManager&     formatManager_;
        juce::AudioThumbnailCache&    thumbnailCache_;
        std::unique_ptr<juce::AudioThumbnail> thumbnail_;

        AudioFilePtr                                 file_;
        std::vector<switchblade::analysis::Transient> transients_;
        switchblade::analysis::SourceClass           classification_
            { switchblade::analysis::SourceClass::Unknown };

        std::optional<float>                         pitchHz_;
        std::optional<float>                         pitchClarity_;

        bool  selected_    { false };
        bool  hovered_     { false };
        bool  loading_     { false };  // "ANALYZING…" overlay
        float liftPhase_   { 0.0f };   // 0 = resting, 1 = fully lifted
        float entryGlow_   { 0.0f };   // 1 = white-hot arrival, decays to 0
        int   draggingIdx_ { -1 };
        std::filesystem::path displayPath_;  // shown before file_ is set

        // Cached mono mixdown for zero-cross snapping during drag.
        // Populated lazily on first drag; cleared when file changes.
        std::vector<float> monoCache_;

        juce::TextButton extractBtn_ { "Extract" };

        juce::VBlankAttachment vblank_;

        JUCE_LEAK_DETECTOR (SampleCard)
    };
} // namespace switchblade::ui
