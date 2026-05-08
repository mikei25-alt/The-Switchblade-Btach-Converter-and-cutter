#include "MainContainer.h"
#include "Core/Palette.h"
#include "Analysis/PitchDetector.h"
#include "Analysis/SliceBoundary.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "BinaryData.h"

#include <algorithm>
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
            g.drawText (juce::String (juce::CharPointer_UTF8 (
                            "WAV  \xc2\xb7  AIFF  \xc2\xb7  MP3  \xc2\xb7  FLAC  \xc2\xb7  OGG")),
                        juce::Rectangle<float> (inner.getX(), cy + 24.0f,
                                               inner.getWidth(), 18.0f).toNearestInt(),
                        juce::Justification::centred, false);
        }
    }

    namespace
    {
        constexpr int kTopBarH      = 80;
        constexpr int kBrandWidth   = 262;   // left brand area: logo + wordmark (painted in paint())
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
        // Filename format:
        //   Melodic (keySuffix set): [stem]_[Note]_[Index].wav  e.g. SerumLead_C#3_01.wav
        //   All other modes:         [stem]_[tag]_[Index].wav   e.g. drums_perc_01.wav
        [[nodiscard]] juce::String makeSliceFilename (
            const juce::String& stem,
            const char* tag,
            const juce::String& keySuffix,
            int index)
        {
            std::ostringstream ss;
            ss << stem.toStdString() << '_';
            if (keySuffix.isNotEmpty())
                ss << keySuffix.toStdString();   // Note replaces tag for Melodic
            else
                ss << tag;
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
        extractAllBtn_.onClick      = [this] { extractAll(); };
        produceBtn_.onClick         = [this] { produceAllSlices(); };
        exportSelectionBtn_.onClick = [this] { exportSelection(); };

        // Right-click on Produce / Export Selection → normalization level picker
        auto showNormMenu = [this]
        {
            juce::PopupMenu menu;
            menu.addItem (1, "No normalization",   true, normTargetDb_ == 0.0f);
            menu.addItem (2, "Normalize to -1 dBFS", true, normTargetDb_ == -1.0f);
            menu.addItem (3, "Normalize to -3 dBFS", true, normTargetDb_ == -3.0f);
            menu.addItem (4, "Normalize to -6 dBFS", true, normTargetDb_ == -6.0f);
            menu.showMenuAsync (juce::PopupMenu::Options{},
                [this] (int result)
                {
                    constexpr float kLevels[] = { 0.0f, -1.0f, -3.0f, -6.0f };
                    if (result >= 1 && result <= 4)
                        setNormTarget (kLevels[result - 1]);
                });
        };
        produceBtn_.onRightClick         = showNormMenu;
        exportSelectionBtn_.onRightClick = showNormMenu;

        addAndMakeVisible (extractAllBtn_);
        addAndMakeVisible (produceBtn_);
        addAndMakeVisible (exportSelectionBtn_);

        // Live "N selected" indicator next to Export Selection
        selectionCountLabel_.setText ("0 selected", juce::dontSendNotification);
        selectionCountLabel_.setColour (juce::Label::textColourId, pal::TextSecondary);
        selectionCountLabel_.setJustificationType (juce::Justification::centredLeft);
        selectionCountLabel_.setFont (juce::Font (juce::FontOptions { 11.0f }).boldened());
        addAndMakeVisible (selectionCountLabel_);

        // Status
        statusLabel_.setText ("Drop audio files to begin.");
        statusLabel_.setColour (pal::TextSecondary);
        statusLabel_.setJustification (juce::Justification::centredLeft);
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
        resultsVault_->onSelectionChanged = [this]
        {
            updateSelectionCount();
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

        // Top bar — 80px tall; 10px vertical padding for controls
        auto bar = area.removeFromTop (kTopBarH);
        bar.reduce (0, 10);

        // Far left: branding painted in paint() — just reserve space
        bar.removeFromLeft (kBrandWidth);
        bar.removeFromLeft (16);  // gap between brand and first control

        // Far right: export buttons, laid out right-to-left then reversed
        bar.removeFromRight (8);  // right edge breathing room
        auto rightButtons = bar.removeFromRight (130 + 6 + 110 + 6 + 130 + 6 + 96);
        extractAllBtn_.setBounds      (rightButtons.removeFromLeft (130));
        rightButtons.removeFromLeft (6);
        produceBtn_.setBounds         (rightButtons.removeFromLeft (110));
        rightButtons.removeFromLeft (6);
        exportSelectionBtn_.setBounds (rightButtons.removeFromLeft (130));
        rightButtons.removeFromLeft (6);
        selectionCountLabel_.setBounds(rightButtons.removeFromLeft (96));

        // Centre: mode combo + sensitivity; status label takes whatever remains
        modeCombo_.setBounds        (bar.removeFromLeft (130));
        bar.removeFromLeft (8);
        sensitivityLabel_.setBounds (bar.removeFromLeft (76));
        bar.removeFromLeft (4);
        sensitivitySlider_.setBounds(bar.removeFromLeft (160));
        bar.removeFromLeft (12);
        statusLabel_.setBounds      (bar);

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

        // Deep charcoal main background
        g.fillAll (pal::MainBackground);

        // ── Header panel — distinctly lighter zone so the bar reads as a surface ─
        {
            // Top: steel-blue-black → bottom: pure MainBackground
            juce::ColourGradient grad {
                juce::Colour (0xFF14171F), 0.0f, 0.0f,
                pal::MainBackground,       0.0f, static_cast<float> (kTopBarH), false };
            g.setGradientFill (grad);
            g.fillRect (0, 0, getWidth(), kTopBarH);
        }

        // ── Branding: Logo + Two-line Wordmark (Top-Left) ────────────────────────
        {
            const auto logo = juce::ImageCache::getFromMemory (
                BinaryData::logo_png, BinaryData::logo_pngSize);

            // Logo geometry — 56px fills the 80px bar with 12px breathing room each side
            constexpr float kLogoPadX = 14.0f;
            constexpr float kLogoSize = 56.0f;
            const float kLogoPadY = (static_cast<float> (kTopBarH) - kLogoSize) * 0.5f;
            const juce::Rectangle<float> badge (kLogoPadX, kLogoPadY, kLogoSize, kLogoSize);

            // Ambient radial glow emanating from logo — gives the header "life"
            {
                const float cx = badge.getCentreX();
                const float cy = badge.getCentreY();
                juce::ColourGradient radial (
                    pal::NeonCyan.withAlpha (0.12f), cx, cy,
                    juce::Colours::transparentBlack, cx + 160.0f, cy,
                    true);
                g.setGradientFill (radial);
                g.fillRect (0, 0, static_cast<int> (cx + 180.0f), kTopBarH);
            }

            // Multi-pass outer glow — 0x8800FBFF (#00FBFF at 53% alpha)
            const juce::Colour logoGlow { 0x8800FBFF };
            for (int i = 6; i >= 1; --i)
            {
                const float r     = static_cast<float> (i) * 1.5f;
                const float alpha = logoGlow.getFloatAlpha()
                                  * (static_cast<float> (i) / 6.0f) * 0.65f;
                g.setColour (logoGlow.withAlpha (alpha));
                g.drawRoundedRectangle (badge.expanded (r), 8.0f + r * 0.3f, r * 0.7f);
            }
            // Crisp inner rim
            g.setColour (pal::NeonCyan.withAlpha (0.70f));
            g.drawRoundedRectangle (badge.expanded (1.0f), 8.0f, 1.5f);

            // Badge body — dark inner gradient so logo pops against it
            {
                juce::ColourGradient fill (
                    juce::Colour (0xFF1A1E2A), badge.getX(),    badge.getY(),
                    juce::Colour (0xFF0C0E14), badge.getRight(), badge.getBottom(),
                    false);
                g.setGradientFill (fill);
                g.fillRoundedRectangle (badge, 7.0f);
            }

            // Subtle inner highlight rim (top-left catch light)
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawRoundedRectangle (badge.reduced (0.5f), 6.5f, 1.0f);

            // Logo image — high-quality resampling
            if (! logo.isNull())
            {
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImageWithin (logo,
                    static_cast<int> (badge.getX() + 2),
                    static_cast<int> (badge.getY() + 2),
                    static_cast<int> (badge.getWidth()  - 4),
                    static_cast<int> (badge.getHeight() - 4),
                    juce::RectanglePlacement::centred
                    | juce::RectanglePlacement::onlyReduceInSize);
                g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);

                // 1.2× brightness lift via white additive overlay
                g.setColour (juce::Colours::white.withAlpha (0.15f));
                g.fillRoundedRectangle (badge.reduced (2.0f), 5.0f);
            }

            // Two-line wordmark — hierarchy like a real product name
            const float textX   = kLogoPadX + kLogoSize + 11.0f;
            const float barHf   = static_cast<float> (kTopBarH);

            // Line 1: "THE" — small, secondary, tracked wide
            {
                const auto labelFont = juce::Font (juce::FontOptions { 9.5f })
                                           .boldened()
                                           .withExtraKerningFactor (0.35f);
                g.setFont (labelFont);
                g.setColour (pal::NeonCyan.withAlpha (0.55f));
                const auto r = juce::Rectangle<float> (textX, barHf * 0.24f,
                                                        160.0f, 14.0f);
                g.drawText ("T H E", r.toNearestInt(),
                            juce::Justification::centredLeft, false);
            }

            // Line 2: "SWITCHBLADE" — large, dominant, near-white
            {
                const auto nameFont = juce::Font (juce::FontOptions { 20.0f })
                                          .boldened()
                                          .withExtraKerningFactor (0.08f);
                g.setFont (nameFont);

                const auto nameR = juce::Rectangle<float> (
                    textX, barHf * 0.44f,
                    static_cast<float> (kBrandWidth) - textX - 4.0f, 26.0f);

                // Cyan glow pass — drawn 1px below for a "lit from behind" effect
                g.setColour (pal::NeonCyan.withAlpha (0.22f));
                g.drawText ("SWITCHBLADE",
                            nameR.translated (0.0f, 1.0f).toNearestInt(),
                            juce::Justification::centredLeft, false);

                // Primary — near-white with the faintest warm tint
                g.setColour (juce::Colour (0xFFF2F6FF));
                g.drawText ("SWITCHBLADE", nameR.toNearestInt(),
                            juce::Justification::centredLeft, false);
            }

            // Vertical separator rule between brand and controls
            const float sepX = static_cast<float> (kBrandWidth) + 8.0f;
            g.setColour (pal::NeonCyan.withAlpha (0.15f));
            g.drawVerticalLine (static_cast<int> (sepX),
                                barHf * 0.15f, barHf * 0.85f);
            g.setColour (pal::ChromeMid.withAlpha (0.25f));
            g.drawVerticalLine (static_cast<int> (sepX) + 1,
                                barHf * 0.15f, barHf * 0.85f);
        }

        // Header bottom border — crisp neon ignition line + hard dark edge
        g.setColour (pal::NeonCyan.withAlpha (0.45f));
        g.drawHorizontalLine (kTopBarH - 1, 0.0f, w);
        g.setColour (pal::HeaderSeparator);
        g.drawHorizontalLine (kTopBarH, 0.0f, w);

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
            card->onMultiSelectChanged = [this] { updateSelectionCount(); };
            card->onModeChangeRequested = [this, rawCard = card.get()]
                (switchblade::analysis::AnalysisMode m) { reAnalyzeCard (rawCard, m); };
            card->setNormDb (normTargetDb_);

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
                juce::String (juce::CharPointer_UTF8 ("ANALYZING\xe2\x80\xa6 ("))
                + juce::String (totalPending) + ")");
            setStatus (juce::String (queued)
                + juce::String (juce::CharPointer_UTF8 (" file(s) queued\xe2\x80\xa6")));
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
                    juce::String (juce::CharPointer_UTF8 ("ANALYZING\xe2\x80\xa6 ("))
                    + juce::String (pendingCards_.size()) + ")");
            return;
        }

        // Update live count on button
        if (analyzing_)
        {
            if (pendingCards_.empty())
                extractAllBtn_.setButtonText ("Extract All");   // last one cleared
            else
                extractAllBtn_.setButtonText (
                    juce::String (juce::CharPointer_UTF8 ("ANALYZING\xe2\x80\xa6 ("))
                    + juce::String (pendingCards_.size()) + ")");
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
            card->onMultiSelectChanged = [this] { updateSelectionCount(); };
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

        // Vault is rebuilt in cards_ order in onAllAnalysisComplete — not here —
        // so tile 001 always corresponds to pad 1, tile 016 to pad V.

        // Wire callbacks that need the loaded data
        card->onExtractClicked = [this, card]
        {
            if (! card->file()) return;
            // "Commit" semantic: re-derive natural ends + onset gap clamp from
            // the current marker positions before exporting, so dragged markers
            // produce correctly-bounded slices instead of inheriting stale
            // naturalEnd values that were computed for the original onsets.
            auto ts = card->transients();
            switchblade::analysis::finalizeSliceBoundaries (*card->file(), ts);
            card->setTransients (std::move (ts));

            renderAndExportCard (*card);
            rebuildVaultFromCards();
        };

        card->onMarkerMoved = [this, card] (int, juce::int64)
        {
            // Per spec: a marker drag must immediately re-render the
            // corresponding slice in the result tiles. We finalise slice
            // boundaries from the new marker positions on the same card,
            // then rebuild the vault so the affected tiles update in place.
            if (card->file())
            {
                auto ts = card->transients();
                switchblade::analysis::finalizeSliceBoundaries (*card->file(), ts);
                card->setTransients (std::move (ts));
            }
            refreshPreviewGrid();
            rebuildVaultFromCards();
        };

        card->onPlayClicked = [this, card]
        {
            if (card->file())
            {
                const auto f = card->file();
                // exclusive=true stops any currently playing voice so each sidebar
                // play click immediately replaces the previous preview.
                previewGrid_->playSlice (f, 0,
                    static_cast<juce::int64> (f->samples.getNumSamples()),
                    true);
            }
        };

        // Algorithm-override dropdown: re-run analysis on this card with a
        // user-selected mode (e.g. force Melodic for a triangle melody that
        // the Auto classifier mis-categorised as Percussive).
        card->onModeChangeRequested = [this, card]
            (switchblade::analysis::AnalysisMode m) { reAnalyzeCard (card, m); };

        const juce::String noteName = result.pitchHz.has_value()
            ? (juce::String (" ") + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"))
               + " " + juce::String (switchblade::analysis::PitchDetector::noteNameFromHz (
                                     *result.pitchHz)))
            : juce::String{};

        setStatus (juce::String (result.path.filename().string())
                   + " — " + juce::String (result.transients.size())
                   + " slices" + noteName
                   + "  |  " + juce::String (cards_.size()) + " loaded");

        // Auto-select if this is the only card; always refresh the full grid
        if (cards_.size() == 1 || selectedCard_ == nullptr)
            selectCard (card);
        else
            refreshPreviewGrid();
    }

    //==========================================================================
    //  Normalization level management
    //==========================================================================
    void MainContainer::setNormTarget (float db)
    {
        normTargetDb_ = db;
        updateNormLabel();
        for (auto& card : cards_)
            card->setNormDb (normTargetDb_);
        if (resultsVault_)
            resultsVault_->setNormMode (normTargetDb_ < 0.0f);
    }

    void MainContainer::updateNormLabel() noexcept
    {
        const bool on = normTargetDb_ < 0.0f;
        const juce::String suffix = on
            ? (" \xe2\x80\xa2 " + juce::String (static_cast<int> (normTargetDb_)) + "dB")
            : juce::String{};
        produceBtn_.setButtonText         ("Produce" + suffix);
        exportSelectionBtn_.setButtonText ("Export Selection" + suffix);
    }

    //==========================================================================
    //  Per-card re-analysis (called from badge dropdown on message thread)
    //==========================================================================
    void MainContainer::reAnalyzeCard (SampleCard* card,
                                        switchblade::analysis::AnalysisMode mode)
    {
        if (card == nullptr || ! card->file()) return;

        // Remove any stale pending-job entry pointing at this card.
        for (auto it = pendingCards_.begin(); it != pendingCards_.end(); )
        {
            if (it->second == card) it = pendingCards_.erase (it);
            else ++it;
        }

        card->setLoading (true);
        const auto path = card->file()->path;
        const int jobId = engine_.enqueue (path, mode);
        pendingCards_.emplace (jobId, card);

        analyzing_ = true;
        extractAllBtn_.setButtonText (
            juce::String (juce::CharPointer_UTF8 ("ANALYZING\xe2\x80\xa6 ("))
            + juce::String (pendingCards_.size()) + ")");
        setStatus (juce::String (path.filename().string())
                   + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x94 re-analysing\xe2\x80\xa6")));
    }

    //==========================================================================
    //  All-analysis-complete handler (message thread)
    //==========================================================================
    void MainContainer::onAllAnalysisComplete()
    {
        analyzing_ = false;
        extractAllBtn_.setButtonText ("Extract All");

        // Rebuild vault in cards_ (drop) order so tile indices always match
        // grid pad indices: tile 001 = pad 1 (key "1"), tile 016 = pad V (key "V").
        if (resultsVault_)
        {
            resultsVault_->clear();
            for (const auto& card : cards_)
            {
                if (! card->file() || card->transients().empty()) continue;

                juce::String noteName;
                if (card->classification() == switchblade::analysis::SourceClass::Melodic
                    && card->pitchHz().has_value()
                    && card->pitchClarity().value_or (0.0f) > 0.5f)
                {
                    noteName = juce::String (
                        switchblade::analysis::PitchDetector::noteNameFromHz (
                            *card->pitchHz()));
                }
                resultsVault_->addSlices (card->file(), card->transients(),
                                          card->classification(), noteName);
            }
            resultsVault_->triggerCompletionCeremony();
        }

        // Multi-selection set was implicitly reset; sync the top-bar counter.
        updateSelectionCount();
        refreshPreviewGrid();

        const int totalCards  = static_cast<int> (cards_.size());
        const int totalSlices = resultsVault_ ? resultsVault_->tileCount()
                                              + resultsVault_->pendingCount() : 0;

        setStatus (juce::String (totalCards)  + " file"
                   + (totalCards  != 1 ? "s" : "")
                   + juce::String (juce::CharPointer_UTF8 ("  \xe2\x80\x94  "))
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
            refreshPreviewGrid();
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

        const std::optional<float> pitchHzExport =
            (card.classification() == switchblade::analysis::SourceClass::Melodic)
            ? card.pitchHz() : std::optional<float> {};

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
            renderSliceToWav (tf, start, std::min (end, len),
                              outDir.getChildFile (fname), pitchHzExport);
        }
        setStatus ("Extracted " + juce::String (ts.size()) + " slices from " + stem);
    }

    void MainContainer::extractAll()
    {
        if (cards_.empty()) { setStatus ("No files loaded."); return; }

        // Match the tile-only selection behaviour in produceAllSlices().
        const bool anySelected =
            resultsVault_ && resultsVault_->selectedTileCount() > 0;

        int total = 0;
        if (anySelected)
        {
            total = resultsVault_->selectedTileCount();
        }
        else
        {
            for (const auto& c : cards_)
                total += static_cast<int> (c->transients().size());
        }

        setStatus ("Extracting " + juce::String (total)
                   + juce::String (juce::CharPointer_UTF8 (" slices\xe2\x80\xa6")));
        produceAllSlices();
    }

    void MainContainer::produceAllSlices()
    {
        if (cards_.empty()) { setStatus ("No files loaded."); return; }

        // Selection-aware: if the user has Neon-Gold-selected any vault tiles,
        // Produce / Extract-All act on that selection only. Sidebar card
        // multi-select doesn't count (per the spec — only vault tiles are
        // eligible for Export Selection).
        const bool anySelected =
            resultsVault_ && resultsVault_->selectedTileCount() > 0;
        if (anySelected) { exportSelection(); return; }

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

            const std::optional<float> cardPitchHz =
                (card->classification() == switchblade::analysis::SourceClass::Melodic)
                ? card->pitchHz() : std::optional<float> {};

            for (std::size_t i = 0; i < ts.size(); ++i)
            {
                const juce::int64 start = ts[i].sampleIndex;
                const juce::int64 end   = (ts[i].naturalEnd > 0)
                    ? ts[i].naturalEnd
                    : ((i + 1 < ts.size()) ? ts[i + 1].sampleIndex : len);
                juce::String ks;
                if (cardPitchHz.has_value())
                    ks = juce::String (switchblade::analysis::PitchDetector::noteNameFromHz (
                                       *cardPitchHz));
                const auto fname = makeSliceFilename (stem, tag, ks, static_cast<int> (i + 1));
                renderSliceToWav (tf, start, std::min (end, len),
                                  outDir.getChildFile (fname), cardPitchHz);
                ++exported;
            }
        }
        setStatus (juce::String (exported) + " slices exported.");
    }

    void MainContainer::exportSelection()
    {
        int exported = 0;

        // Per spec: only Neon-Gold vault tiles are eligible for Export
        // Selection. Sidebar SampleCard multi-select is a visual cue only
        // and does NOT contribute to the export — preventing accidental
        // bulk exports of an entire file when the user really meant to
        // grab a few specific slices from the result vault.
        if (resultsVault_)
        {
            resultsVault_->forEachSelectedTile (
                [this, &exported] (const ResultTile& tile)
                {
                    const auto file = tile.file();
                    if (! file) return;

                    const auto& tf  = *file;
                    const juce::int64 len   = tf.samples.getNumSamples();
                    const juce::int64 start = tile.startSample();
                    const juce::int64 end   = std::min (tile.endSample(), len);

                    const auto stem = juce::File (juce::String (tf.path.string()))
                                          .getFileNameWithoutExtension();
                    const auto outDir = juce::File (juce::String (tf.path.parent_path().string()));
                    const auto tag    = classificationTag (tile.classification());

                    juce::String ks = tile.noteName();
                    std::optional<float> pitchHz;
                    if (tile.classification() == switchblade::analysis::SourceClass::Melodic
                        && ks.isNotEmpty())
                    {
                        // Re-derive Hz from note name lookup is overkill; the ACID metadata
                        // is best-effort here. Caller (analysis) already wrote pitch for
                        // the file; tile-level export uses the note string only for naming.
                    }
                    const auto fname = makeSliceFilename (stem, tag, ks, tile.sliceIndex());
                    renderSliceToWav (tf, start, end,
                                      outDir.getChildFile (fname), pitchHz);
                    ++exported;
                });
        }

        if (exported == 0)
            setStatus ("Nothing selected. Ctrl+click loaded files or vault tiles.");
        else
            setStatus (juce::String (exported) + " slice(s) exported from selection.");
    }

    void MainContainer::renderSliceToWav (
        const switchblade::analysis::AudioFile& file,
        juce::int64 start, juce::int64 end,
        const juce::File& outFile,
        std::optional<float> pitchHz) const
    {
        const int numCh = file.samples.getNumChannels();
        const int numS  = static_cast<int> (end - start);
        if (numS <= 0 || numCh <= 0) return;

        const double sr = file.sampleRate;
        // 5ms fade-in for clean attack; 30ms fade-out for professional one-shot tail
        const int fadeInSamples  = static_cast<int> (std::round (0.005 * sr));
        const int fadeSamples    = static_cast<int> (std::round (0.030 * sr));

        juce::AudioBuffer<float> slice (numCh, numS);
        for (int ch = 0; ch < numCh; ++ch)
            slice.copyFrom (ch, 0, file.samples, ch, static_cast<int> (start), numS);

        for (int ch = 0; ch < numCh; ++ch)
            slice.applyGainRamp (ch, 0, std::min (fadeInSamples, numS), 0.0f, 1.0f);

        const int fadeOutStart = std::max (0, numS - fadeSamples);
        for (int ch = 0; ch < numCh; ++ch)
            slice.applyGainRamp (ch, fadeOutStart, numS - fadeOutStart, 1.0f, 0.0f);

        // Peak normalization — applied after fades so the fade-out doesn't
        // inflate the gain.  Peak is measured post-fade; target is linear.
        if (normTargetDb_ < 0.0f)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* r = slice.getReadPointer (ch);
                for (int si = 0; si < numS; ++si)
                    peak = std::max (peak, std::abs (r[si]));
            }
            if (peak > 1e-5f)   // skip near-silent slices
            {
                const float target = std::pow (10.0f, normTargetDb_ / 20.0f);
                slice.applyGain (target / peak);
            }
        }

        // Build WAV metadata — embed MIDI root note in the ACID chunk so DAWs
        // (Ableton, Logic, FL Studio, etc.) read the pitch on import.
        juce::StringPairArray meta;
        if (pitchHz.has_value() && *pitchHz > 0.0f)
        {
            const int midiNote = std::clamp (
                static_cast<int> (std::round (
                    69.0f + 12.0f * std::log2f (*pitchHz / 440.0f))),
                0, 127);
            meta.set (juce::WavAudioFormat::acidRootSet,  "1");
            meta.set (juce::WavAudioFormat::acidRootNote, juce::String (midiNote));
        }

        juce::WavAudioFormat wav;
        auto* os = outFile.createOutputStream().release();
        if (! os || os->failedToOpen()) { delete os; return; }

        const auto writer = std::unique_ptr<juce::AudioFormatWriter> (
            wav.createWriterFor (os, sr,
                                 static_cast<unsigned int> (numCh),
                                 24, meta, 0));
        if (writer)
            writer->writeFromAudioSampleBuffer (slice, 0, numS);
    }

    //==========================================================================
    //  Helpers
    //==========================================================================
    void MainContainer::updateSelectionCount()
    {
        // Per spec: the counter reflects only Neon-Gold result tiles
        // (the 16-pad vault) — sidebar SampleCards don't contribute to
        // "N selected", so the count always matches the number of WAVs
        // Export Selection will write for the visible vault.
        const int n = resultsVault_ ? resultsVault_->selectedTileCount() : 0;
        selectionCountLabel_.setText (juce::String (n) + " selected",
                                      juce::dontSendNotification);
    }

    void MainContainer::rebuildVaultFromCards()
    {
        if (! resultsVault_) return;

        resultsVault_->clear();
        for (const auto& card : cards_)
        {
            if (! card->file() || card->transients().empty()) continue;

            juce::String noteName;
            if (card->classification() == switchblade::analysis::SourceClass::Melodic
                && card->pitchHz().has_value()
                && card->pitchClarity().value_or (0.0f) > 0.5f)
            {
                noteName = juce::String (
                    switchblade::analysis::PitchDetector::noteNameFromHz (
                        *card->pitchHz()));
            }
            resultsVault_->addSlices (card->file(), card->transients(),
                                      card->classification(), noteName);
        }
        // Selection set was cleared by clear(); refresh the counter so the
        // top bar doesn't keep stale state.
        updateSelectionCount();
    }

    void MainContainer::refreshPreviewGrid()
    {
        std::vector<PreviewGrid::CardData> data;
        data.reserve (cards_.size());
        for (const auto& card : cards_)
        {
            if (card->file())
                data.push_back ({ card->file(), card->transients() });
        }
        previewGrid_->setAllCards (std::move (data));
    }

    void MainContainer::setStatus (const juce::String& msg)
    {
        statusLabel_.setText (msg);
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
