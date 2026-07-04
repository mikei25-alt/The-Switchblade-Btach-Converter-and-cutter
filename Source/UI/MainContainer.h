#pragma once

#include "UI/SampleCard.h"
#include "UI/PreviewGrid.h"
#include "UI/ResultsVault.h"
#include "Analysis/AnalysisEngine.h"
#include "Analysis/PitchDetector.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
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
    //  ClippingLabel — single-line label that CLIPS (no "…", no squashing)
    //  when narrower than its full text; hovering reveals the full string via
    //  tooltip. Used for the top-bar status so a tight layout never distorts
    //  the text.
    //==========================================================================
    class ClippingLabel final : public juce::Component,
                                public juce::SettableTooltipClient
    {
    public:
        ClippingLabel() = default;

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

            // minimumHorizontalScale = 1.0f — never scale text and never add "…".
            // If layout is tight, text simply clips. Per UI policy: no truncation glyphs.
            g.drawFittedText (full_, 0, 0, w, h, just_, 1, 1.0f);
        }

    private:
        juce::String        full_;
        juce::Colour        colour_ { juce::Colours::white };
        juce::Font          font_   { juce::FontOptions { 13.0f } };
        juce::Justification just_   { juce::Justification::centredLeft };

        JUCE_LEAK_DETECTOR (ClippingLabel)
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

        /** Detach a card from the list. Does NOT delete the card —
            ownership stays with MainContainer::cards_. */
        void removeCard (SampleCard* card)
        {
            if (card == nullptr) return;
            removeChildComponent (card);
            cards_.erase (std::remove (cards_.begin(), cards_.end(), card),
                          cards_.end());
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
        bool keyPressed (const juce::KeyPress&) override;

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
        // Grid-only tempo field. Click-to-edit; shows the auto-detected BPM and
        // accepts a manual override. Hidden outside Grid mode.
        juce::Label        bpmField_;
        // Grid-only "Max samples" field. Click-to-edit; "ALL" keeps every grid
        // cell, a number curates to that many strong/distinct one-shots.
        juce::Label        maxSamplesField_;
        // Single primary action (the "Igniter"): exports every slice, or the
        // vault selection when one exists. Doubles as the ANALYZING… (N)
        // progress indicator while jobs are in flight.
        RightClickButton   produceBtn_         { "Produce" };
        RightClickButton   exportSelectionBtn_ { "Export Selection" };
        RightClickButton   outputDirBtn_       { "Source folder" };
        juce::File         outputDir_;                     // empty = same folder as source
        std::unique_ptr<juce::FileChooser> fileChooser_;  // kept alive across async callback
        float              normTargetDb_       { 0.0f };  // 0 = off, negative = target dBFS
        juce::Label        selectionCountLabel_;   // "N selected" — live count
        ClippingLabel      statusLabel_;
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
        int  batchFailures_ { 0 };      // failed jobs since the last all-complete
        // True for the duration of an external drag-out (performExternalDrag-
        // DropOfFiles is blocking on Windows). Guards filesDropped from
        // re-ingesting our own drag as a new source card when the OS drop ends
        // back over this window.
        bool performingDragOut_ { false };

        //----- Private methods -----------------------------------------------
        void onAnalysisCompleted (switchblade::analysis::AnalysisResult result);
        void onAllAnalysisComplete();
        void selectCard (SampleCard* card);
        void reAnalyzeCard (SampleCard* card, switchblade::analysis::AnalysisMode mode);
        void setNormTarget (float db);           // db: 0=off, -1/-3/-6 = target level
        void updateNormLabel() noexcept;         // refreshes button text + card badges
        void chooseOutputDir();                  // opens async folder picker
        void updateOutputDirLabel() noexcept;    // syncs button text/tooltip to outputDir_
        void renderAndExportCard (SampleCard& card);
        void produceAllSlices();

        //----- Background export ----------------------------------------------
        // Everything needed to render one slice to disk, snapshotted on the
        // message thread. Per-slice pitch detection and the WAV write both run
        // on exportPool_ so a big batch never freezes the UI.
        struct ExportSpec
        {
            std::shared_ptr<const switchblade::analysis::AudioFile> file;
            juce::int64                          start { 0 };
            juce::int64                          end   { 0 };
            switchblade::analysis::SourceClass   classification
                { switchblade::analysis::SourceClass::Unknown };
            juce::String                         stem;          // filename stem
            juce::String                         fallbackNote;  // file-wide note
            std::optional<float>                 fallbackPitchHz;
            int                                  index { 1 };
            juce::File                           outDir;
        };
        /** Queue the specs on exportPool_; progress + completion land in the
            status label via callAsync. No-op when a previous export is still
            running (buttons are disabled then anyway). */
        void startBackgroundExport (std::vector<ExportSpec> specs);
        /** Enable/disable Produce + Export Selection based on analyzing_ /
            exporting_ so a half-updated model can't be exported. */
        void updateActionButtonStates();

        juce::ThreadPool                   exportPool_ { 1 };
        std::shared_ptr<std::atomic<bool>> exportCancel_
            { std::make_shared<std::atomic<bool>> (false) };
        bool                               exporting_ { false };

        //----- Drag a card's source file out to the OS / DAW ------------------
        void dragOutCard (SampleCard* card);

        //----- Background drag pre-render -------------------------------------
        // Each landed vault tile is rendered to tempDragDir on a small bg pool
        // so the subsequent drag-out gesture is instant. The path lands back on
        // the tile via Component::SafePointer + callAsync. The pool is held
        // here (not on AnalysisEngine) to keep analysis throughput independent.
        juce::ThreadPool dragRenderPool_ { 2 };
        void queueTilePreRender (ResultTile& tile);

        //----- Sensitivity / Division slider context switch -------------------
        // When Grid mode is selected the slider's range, value, and label
        // change to drive the engine's gridDivisions count instead of the
        // transient detector's sensitivity.
        void applyModeSliderConfig();
        bool sliderInGridMode_ { false };

        //----- Grid tempo (auto-detect + manual override) ---------------------
        // When the user types a BPM into bpmField_ we set bpmUserOverride_ so
        // the auto-detected value stops clobbering their entry. Clearing the
        // field (or typing AUTO/0) returns to auto-detect.
        bool   bpmUserOverride_ { false };
        /** Push a manually-entered BPM into the engine and re-slice grid cards. */
        void   commitBpmField();
        /** Push the Max-samples cap into the engine and re-slice grid cards. */
        void   commitMaxSamplesField();
        /** Show an auto-detected BPM in the field (no-op once user-overridden). */
        void   showDetectedBpm (double bpm);
        /** Re-enqueue every loaded card under the current mode. Runs when the
            mode combo changes and when a Grid tweak (subdivision, BPM, max
            samples) needs already-analysed files re-sliced live. */
        void   reAnalyzeLoadedCards();

        //----- Card deletion --------------------------------------------------
        /** Right-click on a card: show a PopupMenu with "Delete card" or
            "Delete N selected cards", then dispatch to requestDeleteCards. */
        void showCardContextMenu (SampleCard* clickedCard);
        /** Entry point: show context menu (or skip straight to delete when the
            user has previously chosen "Don't ask me again"). */
        void requestDeleteCards (std::vector<SampleCard*> targets);
        /** Actual removal — detaches from CardListComponent, drops from
            cards_, optionally calls ResultsVault::removeSlicesForFile. */
        void deleteCards (const std::vector<SampleCard*>& targets,
                          bool alsoDeleteSlices);
        /** Lazy accessor for the persisted "Don't ask me again" preference. */
        juce::PropertiesFile& userSettings();

        std::unique_ptr<juce::PropertiesFile> userSettings_;
        // Kept alive across the async modal callback — same pattern as fileChooser_.
        std::unique_ptr<juce::AlertWindow>    deletePrompt_;
        std::unique_ptr<juce::ToggleButton>   deletePromptRemember_;
        void renderSliceToWav (const switchblade::analysis::AudioFile& file,
                               juce::int64 start, juce::int64 end,
                               const juce::File& outFile,
                               std::optional<float> pitchHz = {}) const;
        void exportSelection();
        void updateSelectionCount();
        /** Push one card's current transients into the vault in place —
            selection and unrelated tiles survive (unlike a full rebuild). */
        void syncVaultForCard (SampleCard& card);
        void refreshPreviewGrid();
        void setStatus (const juce::String& msg);
        [[nodiscard]] switchblade::analysis::AnalysisMode currentMode() const noexcept;
        [[nodiscard]] float currentSensitivity() const noexcept;
        [[nodiscard]] switchblade::analysis::TransientDetector::Params
                      buildDetectorParams() const noexcept;

        JUCE_LEAK_DETECTOR (MainContainer)
    };
} // namespace switchblade::ui
