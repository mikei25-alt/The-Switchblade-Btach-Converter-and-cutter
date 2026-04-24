#include "MainContainer.h"
#include "Core/Palette.h"
#include "Analysis/PitchDetector.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <filesystem>
#include <sstream>

namespace switchblade::ui
{
    namespace pal = switchblade::palette;

    //==========================================================================
    //  DropZonePanel
    //==========================================================================
    DropZonePanel::DropZonePanel()
        : vblank_ (this, [this] { if (isVisible()) repaint(); })
    {
        setInterceptsMouseClicks (false, false);
    }

    void DropZonePanel::paint (juce::Graphics& g)
    {
        const double t     = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const float  pulse = 0.5f + 0.5f * std::sin (static_cast<float> (t * 1.4));

        const auto area  = getLocalBounds().toFloat().reduced (36.0f, 28.0f);
        const auto inner = area.reduced (2.0f);

        // Outer chrome bevel frame
        {
            juce::Path p;
            p.addRoundedRectangle (area, 20.0f);
            g.setGradientFill (pal::chromeBevel (area, false));
            g.fillPath (p);
        }

        // Recessed body — subtly darker than ChromeVoid
        {
            juce::Path fp;
            fp.addRoundedRectangle (inner, 18.0f);
            juce::ColourGradient grad {
                pal::ChromeVoid.darker (0.18f), inner.getCentreX(), inner.getY(),
                pal::ChromeVoid.darker (0.06f), inner.getCentreX(), inner.getBottom(), false };
            g.setGradientFill (grad);
            g.fillPath (fp);

            // Inner rim shadow
            g.setColour (pal::ChromeDark.withAlpha (0.55f));
            g.drawRoundedRectangle (inner, 18.0f, 1.2f);
        }

        // Pulsing neon inner border
        {
            const auto nr = inner.reduced (10.0f);
            const float alpha = 0.18f + pulse * 0.28f;
            g.setColour (pal::NeonCyan.withAlpha (alpha));
            g.drawRoundedRectangle (nr, 14.0f, 1.4f);

            // Secondary outer glow
            g.setColour (pal::NeonCyan.withAlpha (alpha * 0.4f));
            g.drawRoundedRectangle (nr.expanded (2.0f), 16.0f, 2.5f);
        }

        // Art-Deco corner brackets (NeonGold)
        {
            constexpr float kLeg = 26.0f;
            constexpr float kThk = 1.8f;
            const float cx0 = inner.getX()      + 20.0f;
            const float cy0 = inner.getY()       + 20.0f;
            const float cx1 = inner.getRight()   - 20.0f;
            const float cy1 = inner.getBottom()  - 20.0f;
            const float ga  = 0.50f + pulse * 0.28f;
            g.setColour (pal::NeonGold.withAlpha (ga));
            // TL
            g.drawLine (cx0,        cy0,        cx0 + kLeg, cy0,        kThk);
            g.drawLine (cx0,        cy0,        cx0,        cy0 + kLeg, kThk);
            // TR
            g.drawLine (cx1 - kLeg, cy0,        cx1,        cy0,        kThk);
            g.drawLine (cx1,        cy0,        cx1,        cy0 + kLeg, kThk);
            // BL
            g.drawLine (cx0,        cy1 - kLeg, cx0,        cy1,        kThk);
            g.drawLine (cx0,        cy1,        cx0 + kLeg, cy1,        kThk);
            // BR
            g.drawLine (cx1,        cy1 - kLeg, cx1,        cy1,        kThk);
            g.drawLine (cx1 - kLeg, cy1,        cx1,        cy1,        kThk);
        }

        const float cx = inner.getCentreX();
        const float cy = inner.getCentreY();

        // "THE SWITCHBLADE" wordmark at top of panel
        {
            g.setFont (juce::Font (juce::FontOptions { 11.0f }).boldened());
            g.setColour (pal::ChromeHigh.withAlpha (0.45f));
            g.drawText ("THE  SWITCHBLADE",
                        juce::Rectangle<float> (inner.getX(), inner.getY() + 22.0f,
                                               inner.getWidth(), 16.0f).toNearestInt(),
                        juce::Justification::centred, false);
            // Hairline under wordmark
            const float hy = inner.getY() + 42.0f;
            const float hw = 80.0f;
            g.setColour (pal::ChromeMid.withAlpha (0.30f));
            g.drawLine (cx - hw, hy, cx + hw, hy, 1.0f);
        }

        // Bouncing ↓ arrow
        const float arrowBob = 8.0f * std::sin (static_cast<float> (t * 2.1));
        const float arrowCy  = cy - 44.0f + arrowBob;
        {
            g.setFont (juce::Font (juce::FontOptions { 38.0f }));
            g.setColour (pal::NeonCyan.withAlpha (0.50f + pulse * 0.38f));
            // UTF-8 ↓ (U+2193)
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x93")),
                        juce::Rectangle<float> (cx - 28.0f, arrowCy - 4.0f,
                                               56.0f, 50.0f).toNearestInt(),
                        juce::Justification::centred, false);
        }

        // Horizontal accent lines flanking the arrow
        {
            const float lineY = arrowCy + 20.0f;
            const float gap   = 42.0f;
            const float len   = std::min (110.0f, (inner.getWidth() * 0.5f) - gap - 8.0f);
            g.setColour (pal::NeonCyan.withAlpha (0.15f + pulse * 0.10f));
            g.drawLine (cx - gap - len, lineY, cx - gap, lineY, 1.0f);
            g.drawLine (cx + gap,       lineY, cx + gap + len, lineY, 1.0f);
        }

        // Primary text
        {
            g.setFont (juce::Font (juce::FontOptions { 16.0f }).boldened());
            g.setColour (pal::TextPrimary);
            g.drawText ("DROP AUDIO FILES TO BEGIN",
                        juce::Rectangle<float> (inner.getX(), cy - 6.0f,
                                               inner.getWidth(), 26.0f).toNearestInt(),
                        juce::Justification::centred, false);
        }

        // Format sub-text  (UTF-8 middle dot ·)
        {
            g.setFont (juce::Font (juce::FontOptions { 11.0f }));
            g.setColour (pal::TextSecondary.withAlpha (0.65f));
            g.drawText ("WAV  \xc2\xb7  AIFF  \xc2\xb7  MP3  \xc2\xb7  FLAC  \xc2\xb7  OGG",
                        juce::Rectangle<float> (inner.getX(), cy + 24.0f,
                                               inner.getWidth(), 18.0f).toNearestInt(),
                        juce::Justification::centred, false);
        }
    }

    namespace
    {
        constexpr int kTopBarH      = 48;
        constexpr int kGridFrac     = 30;
        constexpr int kPreviewGridH = 300;   // fixed height for the 4x4 pad grid

        constexpr std::array<const char*, 4> kModeNames {
            "Auto", "Percussive", "Melodic", "Texture"
        };

        [[nodiscard]] const char* classificationTag (
            switchblade::analysis::SourceClass c) noexcept
        {
            using C = switchblade::analysis::SourceClass;
            switch (c)
            {
                case C::Percussive: return "perc";
                case C::Melodic:    return "mel";
                case C::Texture:    return "tex";
                default:            return "unk";
            }
        }

        // std::format portable alternative — avoids MSVC 2019 compatibility risk
        [[nodiscard]] juce::String makeSliceFilename (
            const juce::String& stem,
            const char* tag,
            const juce::String& keySuffix,
            int index)
        {
            std::ostringstream ss;
            ss << stem.toStdString() << '_' << tag;
            if (keySuffix.isNotEmpty())
                ss << '_' << keySuffix.toStdString();
            ss << '_';
            if (index < 100) ss << '0';
            if (index < 10)  ss << '0';
            ss << index << ".wav";
            return juce::String (ss.str());
        }
    } // namespace

    //==========================================================================
    //  Construction / destruction
    //==========================================================================
    MainContainer::MainContainer()
    {
        setWantsKeyboardFocus (true);
        formatManager_.registerBasicFormats();

        // Mode combo
        for (int i = 0; i < static_cast<int> (kModeNames.size()); ++i)
            modeCombo_.addItem (kModeNames[static_cast<std::size_t> (i)], i + 1);
        modeCombo_.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (modeCombo_);

        // Sensitivity slider — wired to engine params on every change
        // Density Guard: max limited to 1.3 to prevent thin/invisible waveforms on short files
        sensitivitySlider_.setRange (0.3, 1.3, 0.01);
        sensitivitySlider_.setValue (1.0, juce::dontSendNotification);
        sensitivitySlider_.setSliderStyle (juce::Slider::LinearHorizontal);
        sensitivitySlider_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
        sensitivitySlider_.onValueChange = [this]
        {
            auto p = buildDetectorParams();
            engine_.setDetectorParams (p);
        };
        addAndMakeVisible (sensitivitySlider_);

        sensitivityLabel_.setText ("Sensitivity", juce::dontSendNotification);
        sensitivityLabel_.setColour (juce::Label::textColourId, pal::TextSecondary);
        sensitivityLabel_.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (sensitivityLabel_);

        // Buttons
        extractAllBtn_.onClick = [this] { extractAll(); };
        produceBtn_.onClick    = [this] { produceAllSlices(); };
        addAndMakeVisible (extractAllBtn_);
        addAndMakeVisible (produceBtn_);

        // Status
        statusLabel_.setText ("Drop audio files to begin.", juce::dontSendNotification);
        statusLabel_.setColour (juce::Label::textColourId, pal::TextSecondary);
        statusLabel_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (statusLabel_);

        // Card viewport
        cardList_ = std::make_unique<CardListComponent> (600);
        cardViewport_.setViewedComponent (cardList_.get(), false);
        cardViewport_.setScrollBarsShown (true, false);
        addAndMakeVisible (cardViewport_);

        // Drop zone — visible until the first card is loaded
        dropZone_ = std::make_unique<DropZonePanel>();
        addAndMakeVisible (*dropZone_);

        // Preview grid — add as key listener on top-level component
        previewGrid_ = std::make_unique<PreviewGrid>();
        addAndMakeVisible (*previewGrid_);
        addKeyListener (previewGrid_.get());

        // Results vault — scrollable jukebox grid of extracted slices
        resultsVault_ = std::make_unique<ResultsVault> (formatManager_, thumbnailCache_);
        resultsViewport_.setViewedComponent (resultsVault_.get(), false);
        resultsViewport_.setScrollBarsShown (true, false);
        addAndMakeVisible (resultsViewport_);

        resultsVault_->onTilePlay = [this] (auto f, auto s, auto e)
        {
            previewGrid_->playSlice (std::move (f), s, e);
        };
        resultsVault_->onTileSelected = [this] (auto f, auto s, auto e)
        {
            previewGrid_->playSlice (std::move (f), s, e);
        };
        resultsVault_->onExportCollection = [this]
        {
            produceAllSlices();
        };

        // Engine callbacks
        engine_.setOnStarted ([this] (int jobId)
        {
            if (auto it = pendingCards_.find (jobId); it != pendingCards_.end())
                it->second->setLoading (true);
        });

        engine_.setOnComplete ([this] (auto result)
        {
            onAnalysisCompleted (std::move (result));
        });

        engine_.setOnAllComplete ([this]
        {
            onAllAnalysisComplete();
        });

        setSize (1280, 800);
    }

    MainContainer::~MainContainer()
    {
        resultsVault_->clear();      // drain timer before children are destroyed
        removeKeyListener (previewGrid_.get());
        audioPlayer_.setSource (nullptr);
        deviceManager_.removeAudioCallback (&audioPlayer_);
    }

    void MainContainer::initAudioDevice()
    {
        deviceManager_.initialiseWithDefaultDevices (0, 2);
        audioPlayer_.setSource (&previewGrid_->getAudioSource());
        deviceManager_.addAudioCallback (&audioPlayer_);
    }

    //==========================================================================
    //  Layout
    //==========================================================================
    void MainContainer::resized()
    {
        auto area = getLocalBounds();

        // Top bar
        auto bar = area.removeFromTop (kTopBarH).reduced (8, 6);
        modeCombo_.setBounds          (bar.removeFromLeft (130));
        bar.removeFromLeft (8);
        sensitivityLabel_.setBounds   (bar.removeFromLeft (76));   // explicit — no attachToComponent
        bar.removeFromLeft (4);
        sensitivitySlider_.setBounds  (bar.removeFromLeft (160));
        bar.removeFromLeft (12);
        extractAllBtn_.setBounds      (bar.removeFromLeft (130));
        bar.removeFromLeft (6);
        produceBtn_.setBounds         (bar.removeFromLeft (110));
        bar.removeFromLeft (12);
        statusLabel_.setBounds        (bar);

        // Body split — right panel: PreviewGrid (top fixed) + ResultsVault (rest)
        const int gridW = area.getWidth() * kGridFrac / 100;
        auto rightPanel = area.removeFromRight (gridW);

        previewGrid_->setBounds (rightPanel.removeFromTop (kPreviewGridH).reduced (8));

        resultsViewport_.setBounds (rightPanel.reduced (4, 0));
        resultsVault_->setViewportWidth (
            resultsViewport_.getWidth() - resultsViewport_.getScrollBarThickness());

        cardViewport_.setBounds (area);
        dropZone_->setBounds (area);           // overlays card viewport when empty
        cardList_->setViewportWidth (area.getWidth());
    }

    void MainContainer::paint (juce::Graphics& g)
    {
        const float w = static_cast<float> (getWidth());
        const float h = static_cast<float> (getHeight());

        // Body fill
        g.fillAll (pal::ChromeVoid);

        // Top-bar chrome gradient
        {
            juce::ColourGradient grad {
                pal::ChromeDark.withAlpha (0.85f), 0.0f,               0.0f,
                pal::ChromeVoid,                   0.0f, static_cast<float> (kTopBarH), false };
            g.setGradientFill (grad);
            g.fillRect (0, 0, getWidth(), kTopBarH);
        }

        // Top-bar bottom line (neon hairline)
        g.setColour (pal::NeonCyan.withAlpha (0.22f));
        g.drawHorizontalLine (kTopBarH,     0.0f, w);
        g.setColour (pal::ChromeMid.withAlpha (0.35f));
        g.drawHorizontalLine (kTopBarH + 1, 0.0f, w);

        // "THE SWITCHBLADE" logotype — drawn in the top-bar, right-aligned
        {
            const auto logoR = juce::Rectangle<float> (w - 190.0f, 0.0f, 180.0f,
                                                       static_cast<float> (kTopBarH));
            g.setFont (juce::Font (juce::FontOptions { 12.5f }).boldened());
            // Two-tone gradient: chrome → neon
            juce::ColourGradient lg { pal::ChromeHigh, logoR.getX(), logoR.getCentreY(),
                                      pal::NeonCyan,  logoR.getRight(), logoR.getCentreY(), false };
            g.setGradientFill (lg);
            g.drawText ("THE  SWITCHBLADE", logoR.toNearestInt(),
                        juce::Justification::centredRight, false);

            // Vertical hairline separator before the logotype
            g.setColour (pal::ChromeMid.withAlpha (0.40f));
            g.drawVerticalLine (static_cast<int> (w - 196.0f),
                                5.0f, static_cast<float> (kTopBarH) - 5.0f);
        }

        // Vertical divider between card area and right panel
        const int gridW = getWidth() * kGridFrac / 100;
        g.setColour (pal::ChromeMid.withAlpha (0.35f));
        g.drawVerticalLine (getWidth() - gridW, static_cast<float> (kTopBarH + 1), h);
        g.setColour (pal::NeonCyan.withAlpha (0.08f));
        g.drawVerticalLine (getWidth() - gridW + 1, static_cast<float> (kTopBarH + 1), h);

        // Drag-over cyan frame
        if (dropHighlight_)
        {
            g.setColour (pal::NeonCyan.withAlpha (0.06f));
            g.fillRect (getLocalBounds());
            g.setColour (pal::NeonCyan.withAlpha (0.50f));
            g.drawRect (getLocalBounds(), 2);
        }
    }

    void MainContainer::mouseDown (const juce::MouseEvent&)
    {
        grabKeyboardFocus();
    }

    //==========================================================================
    //  FileDragAndDropTarget
    //==========================================================================
    bool MainContainer::isInterestedInFileDrag (const juce::StringArray& files)
    {
        for (auto& f : files)
            if (formatManager_.findFormatForFileExtension (
                    juce::File (f).getFileExtension()))
                return true;
        return false;
    }

    void MainContainer::filesDropped (const juce::StringArray& files, int, int)
    {
        dropHighlight_ = false;
        repaint();

        const auto mode = currentMode();
        int queued = 0;

        for (auto& f : files)
        {
            const juce::File jf (f);
            if (! jf.existsAsFile()) continue;

            const auto path = std::filesystem::path (jf.getFullPathName().toStdString());

            // Create an immediate pending card so the user sees activity at once
            auto card = std::make_unique<SampleCard> (formatManager_, thumbnailCache_);
            card->setDisplayPath (path);
            card->setLoading (false);   // onStarted will flip this
            card->onSelected = [this, rawCard = card.get()] { selectCard (rawCard); };

            SampleCard* raw = card.get();
            cardList_->addCard (raw);
            cards_.push_back (std::move (card));
            if (cards_.size() == 1)
                selectCard (raw);

            const int jobId = engine_.enqueue (path, mode);
            pendingCards_.emplace (jobId, raw);
            ++queued;
        }

        if (queued > 0)
        {
            analyzing_ = true;
            const int totalPending = static_cast<int> (pendingCards_.size());
            extractAllBtn_.setButtonText (
                "ANALYZING\xe2\x80\xa6 (" + juce::String (totalPending) + ")");
            setStatus (juce::String (queued) + " file(s) queued\xe2\x80\xa6");
        }
    }

    void MainContainer::fileDragEnter (const juce::StringArray&, int, int)
    {
        dropHighlight_ = true;
        repaint();
    }

    void MainContainer::fileDragExit (const juce::StringArray&)
    {
        dropHighlight_ = false;
        repaint();
    }

    //==========================================================================
    //  Analysis completion (message thread)
    //==========================================================================
    void MainContainer::onAnalysisCompleted (switchblade::analysis::AnalysisResult result)
    {
        // Find the pending card that was created when this job was enqueued
        SampleCard* card = nullptr;
        {
            if (auto it = pendingCards_.find (result.jobId); it != pendingCards_.end())
            {
                card = it->second;
                pendingCards_.erase (it);
            }
        }

        if (! result.ok())
        {
            setStatus ("Error: " + result.errorMessage);
            if (card) card->setLoading (false);
            // Still update the button count — this job is now done
            if (analyzing_ && ! pendingCards_.empty())
                extractAllBtn_.setButtonText (
                    "ANALYZING\xe2\x80\xa6 (" + juce::String (pendingCards_.size()) + ")");
            return;
        }

        // Update live count on button
        if (analyzing_)
        {
            if (pendingCards_.empty())
                extractAllBtn_.setButtonText ("Extract All");   // last one cleared
            else
                extractAllBtn_.setButtonText (
                    "ANALYZING\xe2\x80\xa6 (" + juce::String (pendingCards_.size()) + ")");
        }

        // Load audio into a shared_ptr for card + preview grid
        auto file = switchblade::analysis::loadAudioFile (formatManager_, result.path);
        if (! file.has_value())
        {
            setStatus ("Re-read failed for: "
                       + juce::String (result.path.filename().string()));
            if (card) card->setLoading (false);
            return;
        }

        auto sharedFile = std::make_shared<const switchblade::analysis::AudioFile> (
            std::move (*file));

        // If no pending card existed (e.g. engine was used directly), create one now
        if (card == nullptr)
        {
            auto newCard = std::make_unique<SampleCard> (formatManager_, thumbnailCache_);
            card = newCard.get();
            cardList_->addCard (card);
            cards_.push_back (std::move (newCard));
            card->onSelected = [this, card] { selectCard (card); };
        }

        // Populate
        // Hide drop zone on first card arrival
        if (dropZone_ && dropZone_->isVisible())
            dropZone_->setVisible (false);

        card->setLoading (false);
        card->setFile (sharedFile);
        card->setTransients (result.transients);
        card->setClassification (result.classification);
        card->setPitchHz (result.pitchHz);
        card->setPitchClarity (result.pitchClarity);
        card->triggerEntryGlow();     // "cooling" neon arrival animation

        // Push slices into the results vault (staggered tile arrival)
        resultsVault_->addSlices (sharedFile, result.transients, result.classification);

        // Wire callbacks that need the loaded data
        card->onExtractClicked = [this, card]
        {
            if (card->file())
                renderAndExportCard (*card);
        };

        card->onMarkerMoved = [this, card] (int, juce::int64)
        {
            if (card == selectedCard_)
                previewGrid_->setSource (card->file(), card->transients());
        };

        const juce::String noteName = result.pitchHz.has_value()
            ? (" — " + juce::String (switchblade::analysis::PitchDetector::noteNameFromHz (
                                     *result.pitchHz)))
            : juce::String{};

        setStatus (juce::String (result.path.filename().string())
                   + " — " + juce::String (result.transients.size())
                   + " slices" + noteName
                   + "  |  " + juce::String (cards_.size()) + " loaded");

        // Auto-select if this is the only card
        if (cards_.size() == 1 || selectedCard_ == nullptr)
            selectCard (card);
    }

    //==========================================================================
    //  All-analysis-complete handler (message thread)
    //==========================================================================
    void MainContainer::onAllAnalysisComplete()
    {
        analyzing_ = false;
        extractAllBtn_.setButtonText ("Extract All");

        // Trigger the vault completion ceremony (stamp + export bar slide-in)
        if (resultsVault_)
            resultsVault_->triggerCompletionCeremony();

        const int totalCards  = static_cast<int> (cards_.size());
        const int totalSlices = resultsVault_ ? resultsVault_->tileCount() : 0;

        setStatus (juce::String (totalCards)  + " file"
                   + (totalCards  != 1 ? "s" : "") + " ready  \xe2\x80\x94  "
                   + juce::String (totalSlices) + " slice"
                   + (totalSlices != 1 ? "s" : "") + " extracted");
    }

    //==========================================================================
    //  Card selection
    //==========================================================================
    void MainContainer::selectCard (SampleCard* card)
    {
        if (selectedCard_ == card) return;
        if (selectedCard_) selectedCard_->setSelected (false);
        selectedCard_ = card;
        if (selectedCard_)
        {
            selectedCard_->setSelected (true);
            if (selectedCard_->file())
                previewGrid_->setSource (selectedCard_->file(),
                                         selectedCard_->transients());
        }
        else
        {
            previewGrid_->clear();
        }
    }

    //==========================================================================
    //  Batch rendering helpers
    //==========================================================================
    void MainContainer::renderAndExportCard (SampleCard& card)
    {
        if (! card.file()) return;
        const auto& tf  = *card.file();
        const auto& ts  = card.transients();
        if (ts.empty()) return;

        const juce::int64 len   = tf.samples.getNumSamples();
        const auto stem         = juce::File (juce::String (tf.path.string()))
                                      .getFileNameWithoutExtension();
        const auto outDir       = juce::File (juce::String (tf.path.parent_path().string()));
        const auto tag          = classificationTag (card.classification());

        // For Melodic: key suffix is the detected note name (e.g. "A4", "C#3").
        // TODO: convert from concert pitch to nearest chromatic key for full Grain of Salt naming.
        juce::String keySuffix;
        if (card.classification() == switchblade::analysis::SourceClass::Melodic
            && card.pitchHz().has_value())
        {
            keySuffix = juce::String (switchblade::analysis::PitchDetector::noteNameFromHz (
                *card.pitchHz()));
        }

        for (std::size_t i = 0; i < ts.size(); ++i)
        {
            const juce::int64 start = ts[i].sampleIndex;
            // Use energy-decay natural end to preserve the full ADSR tail.
            // Falls back to next onset only when naturalEnd is not set.
            const juce::int64 end = (ts[i].naturalEnd > 0)
                ? ts[i].naturalEnd
                : ((i + 1 < ts.size()) ? ts[i + 1].sampleIndex : len);
            const auto fname = makeSliceFilename (stem, tag, keySuffix,
                                                  static_cast<int> (i + 1));
            renderSliceToWav (tf, start, std::min (end, len), outDir.getChildFile (fname));
        }
        setStatus ("Extracted " + juce::String (ts.size()) + " slices from " + stem);
    }

    void MainContainer::extractAll()
    {
        if (cards_.empty()) { setStatus ("No files loaded."); return; }
        int total = 0;
        for (const auto& c : cards_)
            total += static_cast<int> (c->transients().size());
        setStatus ("Extracting " + juce::String (total) + " slices\xe2\x80\xa6");
        produceAllSlices();
    }

    void MainContainer::produceAllSlices()
    {
        if (cards_.empty()) { setStatus ("No files loaded."); return; }
        int exported = 0;
        for (const auto& card : cards_)
        {
            if (! card->file() || card->transients().empty()) continue;
            const auto& tf  = *card->file();
            const auto& ts  = card->transients();
            const juce::int64 len = tf.samples.getNumSamples();
            const auto stem  = juce::File (juce::String (tf.path.string()))
                                   .getFileNameWithoutExtension();
            const auto outDir = juce::File (juce::String (tf.path.parent_path().string()));
            const auto tag    = classificationTag (card->classification());

            for (std::size_t i = 0; i < ts.size(); ++i)
            {
                const juce::int64 start = ts[i].sampleIndex;
                const juce::int64 end   = (ts[i].naturalEnd > 0)
                    ? ts[i].naturalEnd
                    : ((i + 1 < ts.size()) ? ts[i + 1].sampleIndex : len);
                juce::String ks;
                if (card->classification() == switchblade::analysis::SourceClass::Melodic
                    && card->pitchHz().has_value())
                    ks = juce::String (switchblade::analysis::PitchDetector::noteNameFromHz (
                                       *card->pitchHz()));
                const auto fname = makeSliceFilename (stem, tag, ks, static_cast<int> (i + 1));
                renderSliceToWav (tf, start, std::min (end, len), outDir.getChildFile (fname));
                ++exported;
            }
        }
        setStatus (juce::String (exported) + " slices exported.");
    }

    void MainContainer::renderSliceToWav (
        const switchblade::analysis::AudioFile& file,
        juce::int64 start, juce::int64 end,
        const juce::File& outFile) const
    {
        const int numCh = file.samples.getNumChannels();
        const int numS  = static_cast<int> (end - start);
        if (numS <= 0 || numCh <= 0) return;

        const double sr = file.sampleRate;
        const int fadeSamples = static_cast<int> (std::round (0.005 * sr));

        juce::AudioBuffer<float> slice (numCh, numS);
        for (int ch = 0; ch < numCh; ++ch)
            slice.copyFrom (ch, 0, file.samples, ch, static_cast<int> (start), numS);

        for (int ch = 0; ch < numCh; ++ch)
            slice.applyGainRamp (ch, 0, std::min (fadeSamples, numS), 0.0f, 1.0f);

        const int fadeOutStart = std::max (0, numS - fadeSamples);
        for (int ch = 0; ch < numCh; ++ch)
            slice.applyGainRamp (ch, fadeOutStart, numS - fadeOutStart, 1.0f, 0.0f);

        juce::WavAudioFormat wav;
        auto* os = outFile.createOutputStream().release();
        if (! os || os->failedToOpen()) { delete os; return; }

        const auto writer = std::unique_ptr<juce::AudioFormatWriter> (
            wav.createWriterFor (os, sr,
                                 static_cast<unsigned int> (numCh),
                                 24, {}, 0));
        if (writer)
            writer->writeFromAudioSampleBuffer (slice, 0, numS);
    }

    //==========================================================================
    //  Helpers
    //==========================================================================
    void MainContainer::setStatus (const juce::String& msg)
    {
        statusLabel_.setText (msg, juce::dontSendNotification);
    }

    switchblade::analysis::AnalysisMode MainContainer::currentMode() const noexcept
    {
        using M = switchblade::analysis::AnalysisMode;
        switch (modeCombo_.getSelectedId())
        {
            case 2:  return M::Percussive;
            case 3:  return M::Melodic;
            case 4:  return M::Texture;
            default: return M::Auto;
        }
    }

    float MainContainer::currentSensitivity() const noexcept
    {
        return static_cast<float> (sensitivitySlider_.getValue());
    }

    switchblade::analysis::TransientDetector::Params
    MainContainer::buildDetectorParams() const noexcept
    {
        switchblade::analysis::TransientDetector::Params p;
        p.sensitivity = currentSensitivity();
        return p;
    }
} // namespace switchblade::ui
