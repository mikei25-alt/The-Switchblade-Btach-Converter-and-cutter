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
    //  EllipsisLabel — single-line label that truncates with "…" when narrower
    //  than its full text. Hovering reveals the full string via tooltip. Used
    //  for the top-bar status so it never overruns the SWITCHBLADE wordmark.
    //==========================================================================
    class EllipsisLabel final : public juce::Component,
                                public juce::SettableTooltipClient
    {
    public:
        EllipsisLabel() = default;

        void setText (const juce::String& s)
        {
            full_ = s;
            setTooltip (full_);
            repaint();
        }

        void setColour (juce::Colour c)              { colour_ = c; repaint(); }
        void setFont   (juce::Font f)                { font_   = std::move (f); repaint(); }
        void setJustification (juce::Justification j){ just_   = j; repaint(); }

        void paint (juce::Graphics& g) override
        {
            g.setColour (colour_);
            g.setFont (font_);

            const int w = getWidth();
            const int h = getHeight();
            if (w <= 0 || h <= 0 || full_.isEmpty()) return;

            auto measure = [this] (const juce::String& s) -> float
            {
                juce::GlyphArrangement ga;
                ga.addLineOfText (font_, s, 0.0f, 0.0f);
                return ga.getBoundingBox (0, -1, true).getWidth();
            };

            if (measure (full_) <= static_cast<float> (w))
            {
                g.drawText (full_, 0, 0, w, h, just_, false);
                return;
            }

            // Binary-truncate from the right until "string…" fits.
            const juce::String ell = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
            int lo = 0, hi = full_.length();
            while (lo < hi)
            {
                const int mid = (lo + hi + 1) / 2;
                const auto candidate = full_.substring (0, mid) + ell;
                if (measure (candidate) <= static_cast<float> (w))
                    lo = mid;
                else
                    hi = mid - 1;
            }
            g.drawText (full_.substring (0, lo) + ell, 0, 0, w, h, just_, false);
        }

    private:
        juce::String        full_;
        juce::Colour        colour_ { juce::Colours::white };
        juce::Font          font_   { juce::FontOptions { 13.0f } };
        juce::Justification just_   { juce::Justification::centredLeft };

        JUCE_LEAK_DETECTOR (EllipsisLabel)
    };

    //==========================================================================
    //  RightClickButton — TextButton that also fires a callback on right-click.
    //  Used for Produce / Export Selection so a right-click opens the
    //  normalization level picker without adding any visible controls.
    //==========================================================================
    class RightClickButton final : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;
        std::function<void()> onRightClick;

    protected:
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isRightButtonDown())
            {
                if (onRightClick) onRightClick();
                return;
            }
            juce::TextButton::mouseDown (e);
        }
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
        juce::TextButton   extractAllBtn_      { "Extract All" };
        RightClickButton   produceBtn_         { "Produce" };
        RightClickButton   exportSelectionBtn_ { "Export Selection" };
        float              normTargetDb_       { 0.0f };  // 0 = off, negative = target dBFS
        juce::Label        selectionCountLabel_;   // "N selected" — live count
        EllipsisLabel      statusLabel_;
        juce::TooltipWindow tooltipWindow_ { this, 600 };

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
        void reAnalyzeCard (SampleCard* card, switchblade::analysis::AnalysisMode mode);
        void setNormTarget (float db);     // db: 0=off, -1/-3/-6 = target level
        void updateNormLabel() noexcept;   // refreshes button text + card badges
        void renderAndExportCard (SampleCard& card);
        void extractAll();
        void produceAllSlices();
        void renderSliceToWav (const switchblade::analysis::AudioFile& file,
                               juce::int64 start, juce::int64 end,
                               const juce::File& outFile,
                               std::optional<float> pitchHz = {}) const;
        void exportSelection();
        void updateSelectionCount();
        void rebuildVaultFromCards();
        void refreshPreviewGrid();
        void setStatus (const juce::String& msg);
        [[nodiscard]] switchblade::analysis::AnalysisMode currentMode() const noexcept;
        [[nodiscard]] float currentSensitivity() const noexcept;
        [[nodiscard]] switchblade::analysis::TransientDetector::Params
                      buildDetectorParams() const noexcept;

        JUCE_LEAK_DETECTOR (MainContainer)
    };
} // namespace switchblade::ui
