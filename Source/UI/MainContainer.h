#pragma once

#include "UI/SampleCard.h"
#include "UI/PreviewGrid.h"
#include "UI/ResultsVault.h"
#include "Analysis/AnalysisEngine.h"
#include "Analysis/PitchDetector.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace switchblade::ui
{
    //==========================================================================
    //  DropZonePanel — animated overlay displayed before any files are loaded.
    //  Shows a recessed chrome "feeder slot" with a bouncing neon arrow and
    //  Art-Deco corner brackets. Self-animates via VBlankAttachment at 60 fps.
    //  Intercepting clicks is disabled so drag-and-drop reaches MainContainer.
    //==========================================================================
    class DropZonePanel final : public juce::Component
    {
    public:
        DropZonePanel();
        void paint (juce::Graphics& g) override;

    private:
        juce::VBlankAttachment vblank_;
        JUCE_LEAK_DETECTOR (DropZonePanel)
    };

    //==========================================================================
    //  CardListComponent — inner content Component for the Viewport.
    //  Stretches vertically as cards are added; horizontal layout is fixed.
    //==========================================================================
    class CardListComponent final : public juce::Component
    {
    public:
        static constexpr int kCardH  = 160;
        static constexpr int kCardGap = 8;

        explicit CardListComponent (int viewportWidth) : viewW_ (viewportWidth) {}

        void addCard (SampleCard* card)
        {
            addAndMakeVisible (*card);
            cards_.push_back (card);
            relayout();
        }

        void setViewportWidth (int w) { viewW_ = w; relayout(); }

        void relayout()
        {
            const int n = static_cast<int> (cards_.size());
            const int totalH = n * (kCardH + kCardGap) + kCardGap;
            setSize (viewW_, std::max (1, totalH));

            for (int i = 0; i < n; ++i)
                cards_[static_cast<std::size_t> (i)]->setBounds (
                    kCardGap, kCardGap + i * (kCardH + kCardGap),
                    viewW_ - 2 * kCardGap, kCardH);
        }

    private:
        std::vector<SampleCard*> cards_;
        int viewW_;

        JUCE_LEAK_DETECTOR (CardListComponent)
    };

    //==========================================================================
    //  MainContainer
    //==========================================================================
    class MainContainer final : public juce::Component,
                                public juce::FileDragAndDropTarget
    {
    public:
        MainContainer();
        ~MainContainer() override;

        MainContainer (const MainContainer&) = delete;
        MainContainer& operator= (const MainContainer&) = delete;

        //----- Component ------------------------------------------------------
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

        //----- FileDragAndDropTarget ------------------------------------------
        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void filesDropped (const juce::StringArray& files, int x, int y) override;
        void fileDragEnter (const juce::StringArray&, int, int) override;
        void fileDragExit  (const juce::StringArray&) override;

        //------ Audio device initialization -----------------------------------
        /** Called from Main.cpp after construction. */
        void initAudioDevice();

    private:
        //----- Infrastructure ------------------------------------------------
        juce::AudioFormatManager                       formatManager_;
        juce::AudioThumbnailCache                      thumbnailCache_ { 64 };
        juce::AudioDeviceManager                       deviceManager_;
        juce::AudioSourcePlayer                        audioPlayer_;
        switchblade::analysis::AnalysisEngine          engine_ { 4 };

        //----- Top-bar controls ----------------------------------------------
        juce::ComboBox     modeCombo_;
        juce::Slider       sensitivitySlider_;
        juce::Label        sensitivityLabel_;
        juce::TextButton   extractAllBtn_  { "Extract All" };
        juce::TextButton   produceBtn_     { "Produce" };
        juce::Label        statusLabel_;

        //----- Card list ------------------------------------------------------
        juce::Viewport                           cardViewport_;
        std::unique_ptr<CardListComponent>       cardList_;
        std::vector<std::unique_ptr<SampleCard>> cards_;
        SampleCard*                              selectedCard_ { nullptr };

        //----- Preview grid --------------------------------------------------
        std::unique_ptr<PreviewGrid>             previewGrid_;

        //----- Results vault — jukebox grid of extracted slices --------------
        juce::Viewport                           resultsViewport_;
        std::unique_ptr<ResultsVault>            resultsVault_;

        //----- Pending-card map: jobId → card awaiting analysis result --------
        std::unordered_map<int, SampleCard*> pendingCards_;

        //----- Drop zone (shown before any files are loaded) ------------------
        std::unique_ptr<DropZonePanel>           dropZone_;

        //----- Runtime state -------------------------------------------------
        bool dropHighlight_ { false };
        bool analyzing_     { false };  // true while any jobs are in-flight

        //----- Private methods -----------------------------------------------
        void onAnalysisCompleted (switchblade::analysis::AnalysisResult result);
        void onAllAnalysisComplete();
        void selectCard (SampleCard* card);
        void renderAndExportCard (SampleCard& card);
        void extractAll();
        void produceAllSlices();
        void renderSliceToWav (const switchblade::analysis::AudioFile& file,
                               juce::int64 start, juce::int64 end,
                               const juce::File& outFile) const;
        void setStatus (const juce::String& msg);
        [[nodiscard]] switchblade::analysis::AnalysisMode currentMode() const noexcept;
        [[nodiscard]] float currentSensitivity() const noexcept;
        [[nodiscard]] switchblade::analysis::TransientDetector::Params
                      buildDetectorParams() const noexcept;

        JUCE_LEAK_DETECTOR (MainContainer)
    };
} // namespace switchblade::ui
