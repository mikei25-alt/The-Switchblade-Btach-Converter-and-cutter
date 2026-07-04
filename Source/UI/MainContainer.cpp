#include "MainContainer.h"
#include "Core/Palette.h"
#include "Analysis/BwavMeta.h"
#include "Analysis/PitchDetector.h"
#include "Analysis/SliceBoundary.h"
#include "Analysis/SliceFades.h"

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
        //==========================================================================
        //  SimpleCheckbox — paints only the indicator box. The accompanying
        //  text label is provided by AlertWindow itself (it auto-renders each
        //  custom component's getName() 14 px above the component, in the
        //  dialog's standard label font), so painting our own text here would
        //  just duplicate it.
        //==========================================================================
        class SimpleCheckbox final : public juce::ToggleButton
        {
        public:
            using juce::ToggleButton::ToggleButton;

            void paintButton (juce::Graphics& g, bool isOver, bool /*isDown*/) override
            {
                const auto bounds = getLocalBounds().toFloat();
                constexpr float kBox = 14.0f;
                const juce::Rectangle<float> box {
                    0.0f, (bounds.getHeight() - kBox) * 0.5f, kBox, kBox };

                g.setColour (juce::Colours::white.withAlpha (isOver ? 0.95f : 0.70f));
                g.drawRoundedRectangle (box, 2.0f, 1.2f);

                if (getToggleState())
                {
                    g.setColour (juce::Colour (0xFF00C8FF));   // Switchblade cyan
                    g.fillRoundedRectangle (box.reduced (3.0f), 1.5f);
                }
            }
        };

        constexpr int kTopBarH      = 80;
        constexpr int kBrandWidth   = 235;   // left brand area: logo (50) + 15 gap + wordmark (~170)
        constexpr int kHeaderPadX   = 20;    // outer L/R padding so content doesn't choke window edge
        constexpr int kGridFrac     = 30;
        constexpr int kPreviewGridH = 300;   // preferred height for the 4x4 pad grid

        // The multi-select modifier is Cmd on macOS (Ctrl+click is the popup
        // gesture there) and Ctrl everywhere else — keep hint text honest.
       #if JUCE_MAC
        constexpr const char* kMultiSelectKeyName = "Cmd";
       #else
        constexpr const char* kMultiSelectKeyName = "Ctrl";
       #endif

        constexpr std::array<const char*, 5> kModeNames {
            "Auto", "Percussive", "Melodic", "Texture", "Grid"
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
                case C::Grid:       return "grid";
                default:            return "unk";
            }
        }

        // wantsPitch is defined in Analysis/AnalysisEngine.h (public, single
        // source of truth) — call sites below use ADL to resolve it via the
        // SourceClass argument's namespace.

        //----- Grid musical-subdivision stops ----------------------------------
        // Powers of 2 plus the four common triplet positions, ordered ascending:
        //
        //   2  =  1/2        |   16 = 1/16
        //   4  =  1/4        |   24 = 1/16T   (triplet sixteenths, 4 beats × 6)
        //   6  =  1/4T       |   32 = 1/32
        //   8  =  1/8        |   48 = 1/32T   (triplet 32nds,      4 beats × 12)
        //  12  =  1/8T       |   64 = 1/64
        //
        // The slider snaps to these 10 values only; anything between (e.g. 13
        // or 21) isn't a meaningful musical subdivision.
        inline constexpr std::array<int, 10> kGridStops {
            2, 4, 6, 8, 12, 16, 24, 32, 48, 64
        };

        [[nodiscard]] inline juce::String gridStopLabel (int n)
        {
            switch (n)
            {
                case  6: return "1/4T";
                case 12: return "1/8T";
                case 24: return "1/16T";
                case 48: return "1/32T";
                default: return "1/" + juce::String (n);
            }
        }

        // std::format portable alternative — avoids MSVC 2019 compatibility risk
        // Filename format:
        //   Melodic (keySuffix set): [stem]_[Note(+/-cc c)]_[Index].wav
        //                            e.g. SerumLead_C#3_01.wav, SerumLead_C#3+12c_02.wav
        //   All other modes:         [stem]_[tag]_[Index].wav   e.g. drums_perc_01.wav
        // The cents suffix is signed and zero-padded to 2 digits, and is
        // omitted entirely when the slice is within ±0.5 cents of equal
        // temperament (keeps in-tune filenames clean).
        // detectSlicePitchHz lives in switchblade::analysis (PitchDetector.h)
        // — used here in three export paths AND inside ResultsVault::addSlices
        // so the per-tile vault badges show the correct per-slice note.
        using switchblade::analysis::detectSlicePitchHz;

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

        //----- Drag-to-DAW temp file plumbing ----------------------------------
        // All drag-out renders go into a dedicated subdir of the OS temp folder
        // so we can sweep stale files cleanly at startup (and never accidentally
        // delete unrelated temp files in the broader %TEMP% directory).
        [[nodiscard]] juce::File tempDragDir()
        {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("Switchblade");
            dir.createDirectory();
            return dir;
        }

        // Filename for a drag-out temp render — mirrors the export-pipeline
        // naming exactly (cents-aware for melodic slices) so the clip name
        // that lands in the DAW is the meaningful one. pitchHz is returned
        // so the caller can forward it to renderSliceToWav for any pitch-
        // dependent processing (currently unused, but kept symmetrical with
        // the export path).
        struct TileRenderName
        {
            juce::String         filename;
            std::optional<float> pitchHz;
        };

        [[nodiscard]] TileRenderName buildTileFilename (const ResultTile& tile)
        {
            TileRenderName out;
            if (! tile.file()) return out;

            const auto& tf  = *tile.file();
            const juce::int64 totalLen = tf.samples.getNumSamples();
            const juce::int64 start    = tile.startSample();
            const juce::int64 end      = std::min (tile.endSample(), totalLen);
            if (end <= start) return out;

            const auto stem = juce::File (juce::String (tf.path.string()))
                                 .getFileNameWithoutExtension();
            const auto tag  = classificationTag (tile.classification());

            juce::String keySuffix;
            if (wantsPitch (tile.classification()))
            {
                out.pitchHz = detectSlicePitchHz (tf, start, end);
                if (out.pitchHz.has_value())
                    keySuffix = juce::String (
                        switchblade::analysis::PitchDetector::noteNameWithCentsFromHz (
                            *out.pitchHz));
                else
                    keySuffix = tile.noteName();   // fallback (file-wide note, no cents)
            }

            out.filename = makeSliceFilename (stem, tag, keySuffix, tile.sliceIndex());
            return out;
        }

        //----------------------------------------------------------------------
        // renderSliceToWavImpl — anonymous-namespace free function so background
        // pre-render jobs can call it without capturing `this`. The member
        // MainContainer::renderSliceToWav is now a thin wrapper that forwards
        // the current normTargetDb_ snapshot. Body is byte-identical to the
        // member's previous implementation (peak normalize, micro-fades, ACID
        // root note, BWAV bext chunk, 24-bit WAV write).
        //----------------------------------------------------------------------
        void renderSliceToWavImpl (const switchblade::analysis::AudioFile& file,
                                   juce::int64 start, juce::int64 end,
                                   const juce::File& outFile,
                                   std::optional<float> pitchHz,
                                   float normTargetDb)
        {
            const int numCh = file.samples.getNumChannels();
            const int numS  = static_cast<int> (end - start);
            if (numS <= 0 || numCh <= 0) return;

            const double sr = file.sampleRate;
            const auto   fades = switchblade::analysis::sliceFades (numS, sr);

            juce::AudioBuffer<float> slice (numCh, numS);
            for (int ch = 0; ch < numCh; ++ch)
                slice.copyFrom (ch, 0, file.samples, ch, static_cast<int> (start), numS);

            if (fades.fadeInSamples > 0)
                for (int ch = 0; ch < numCh; ++ch)
                    slice.applyGainRamp (ch, 0, fades.fadeInSamples, 0.0f, 1.0f);

            if (fades.fadeOutSamples > 0)
                for (int ch = 0; ch < numCh; ++ch)
                    slice.applyGainRamp (ch, numS - fades.fadeOutSamples,
                                         fades.fadeOutSamples, 1.0f, 0.0f);

            if (normTargetDb < 0.0f)
            {
                float peak = 0.0f;
                for (int ch = 0; ch < numCh; ++ch)
                {
                    const float* r = slice.getReadPointer (ch);
                    for (int si = 0; si < numS; ++si)
                        peak = std::max (peak, std::abs (r[si]));
                }
                if (peak > 1e-5f)
                {
                    const float target = std::pow (10.0f, normTargetDb / 20.0f);
                    slice.applyGain (target / peak);
                }
            }

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
            {
                const auto bwav = switchblade::analysis::buildBwavMeta (
                    file.path, outFile.getFileNameWithoutExtension(), start);
                for (const auto& key : bwav.getAllKeys())
                    meta.set (key, bwav[key]);
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
    } // namespace

    //==========================================================================
    //  Construction / destruction
    //==========================================================================
    MainContainer::MainContainer()
    {
        setWantsKeyboardFocus (true);

        // Sweep any drag-out temp files left behind by a previous crash or
        // unclean shutdown. Per-drag cleanup also runs via the JUCE drag
        // callback, but this is the safety net for the not-so-happy path.
        {
            auto dir = tempDragDir();
            for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.wav"))
                f.deleteFile();
        }

        formatManager_.registerBasicFormats();
       #if JUCE_WINDOWS
        formatManager_.registerFormat (new juce::WindowsMediaAudioFormat(), false);
       #endif

        // Mode combo
        for (int i = 0; i < static_cast<int> (kModeNames.size()); ++i)
            modeCombo_.addItem (kModeNames[static_cast<std::size_t> (i)], i + 1);
        modeCombo_.setSelectedId (1, juce::dontSendNotification);
        modeCombo_.onChange = [this]
        {
            applyModeSliderConfig();
            // The mode applies to already-loaded cards too. Previously only
            // Grid tweaks re-sliced live, so switching Auto -> Percussive
            // silently left every existing card under the old mode.
            reAnalyzeLoadedCards();
        };
        addAndMakeVisible (modeCombo_);

        // Sensitivity slider — wired to engine params on every change
        // Density Guard: max limited to 1.3 to prevent thin/invisible waveforms on short files
        sensitivitySlider_.setRange (0.3, 1.3, 0.01);
        sensitivitySlider_.setValue (1.0, juce::dontSendNotification);
        sensitivitySlider_.setSliderStyle (juce::Slider::LinearHorizontal);
        sensitivitySlider_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sensitivitySlider_.onValueChange = [this]
        {
            if (sliderInGridMode_)
            {
                // Snap to the nearest musical subdivision (kGridStops in the
                // anonymous namespace). 10 stops: powers of 2 + triplets.
                const double v = sensitivitySlider_.getValue();
                int best = kGridStops.front();
                double bestDist = std::abs (v - static_cast<double> (best));
                for (auto s : kGridStops)
                {
                    const double d = std::abs (v - static_cast<double> (s));
                    if (d < bestDist) { best = s; bestDist = d; }
                }
                if (std::abs (v - static_cast<double> (best)) > 0.5)
                    sensitivitySlider_.setValue (
                        static_cast<double> (best), juce::dontSendNotification);

                engine_.setGridDivisions (best);
                sensitivityLabel_.setText (gridStopLabel (best),
                                           juce::dontSendNotification);
            }
            else
            {
                auto p = buildDetectorParams();
                engine_.setDetectorParams (p);
            }
        };
        addAndMakeVisible (sensitivitySlider_);

        sensitivityLabel_.setText ("SENS", juce::dontSendNotification);
        sensitivityLabel_.setColour (juce::Label::textColourId, juce::Colour (0xFF9A9A9A));
        sensitivityLabel_.setJustificationType (juce::Justification::centredLeft);
        sensitivityLabel_.setFont (juce::Font (juce::FontOptions { 10.0f })
                                       .boldened()
                                       .withExtraKerningFactor (0.20f));
        sensitivityLabel_.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (sensitivityLabel_);

        // Grid-only tempo field. Single-click to edit; "AUTO" = auto-detect.
        // A typed number overrides detection so the grid lays exact quarter-note
        // boundaries at that BPM.
        bpmField_.setEditable (true);
        bpmField_.setJustificationType (juce::Justification::centred);
        bpmField_.setColour (juce::Label::textColourId,       juce::Colour (0xFFE8C76B));
        bpmField_.setColour (juce::Label::backgroundColourId, juce::Colour (0x22FFFFFF));
        bpmField_.setFont (juce::Font (juce::FontOptions { 10.0f })
                               .boldened()
                               .withExtraKerningFactor (0.10f));
        bpmField_.setTooltip ("Grid tempo (BPM). Auto-detected from the audio; "
                              "click to type a BPM to override, or clear it for AUTO.");
        bpmField_.setText ("AUTO", juce::dontSendNotification);
        bpmField_.onTextChange = [this] { commitBpmField(); };
        bpmField_.setVisible (false);   // shown only in Grid mode
        addAndMakeVisible (bpmField_);

        // Grid-only "Max samples" cap. "ALL" keeps every grid cell; a number
        // curates to that many strong/distinct one-shots.
        maxSamplesField_.setEditable (true);
        maxSamplesField_.setJustificationType (juce::Justification::centred);
        maxSamplesField_.setColour (juce::Label::textColourId,       juce::Colour (0xFFE8C76B));
        maxSamplesField_.setColour (juce::Label::backgroundColourId, juce::Colour (0x22FFFFFF));
        maxSamplesField_.setFont (juce::Font (juce::FontOptions { 10.0f })
                                      .boldened()
                                      .withExtraKerningFactor (0.10f));
        maxSamplesField_.setTooltip ("Max one-shots Grid keeps. ALL = one per "
                                     "subdivision; a number keeps that many "
                                     "strongest, most distinct hits.");
        maxSamplesField_.setText ("ALL", juce::dontSendNotification);
        maxSamplesField_.onTextChange = [this] { commitMaxSamplesField(); };
        maxSamplesField_.setVisible (false);   // shown only in Grid mode
        addAndMakeVisible (maxSamplesField_);

        // Re-slice grid cards live when the subdivision changes (on drag end so
        // we don't re-enqueue on every intermediate slider value).
        sensitivitySlider_.onDragEnd = [this]
        {
            if (sliderInGridMode_)
                reAnalyzeLoadedCards();
        };

        // Buttons
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

        // Surface the hidden right-click menu — a tooltip is the minimum
        // discoverable cue for an option that changes what lands on disk.
        produceBtn_.setTooltip ("Export every slice (or the vault selection when "
                                "one exists).\nRight-click: normalization level.");
        exportSelectionBtn_.setTooltip (
            juce::String (kMultiSelectKeyName)
            + "+click vault tiles to select, then export just those.\n"
              "Right-click: normalization level.");

        addAndMakeVisible (produceBtn_);
        addAndMakeVisible (exportSelectionBtn_);

        outputDirBtn_.onClick      = [this] { chooseOutputDir(); };
        outputDirBtn_.onRightClick = [this] { outputDir_ = juce::File{}; updateOutputDirLabel(); };
        updateOutputDirLabel();
        addAndMakeVisible (outputDirBtn_);

        // Live "N selected" indicator next to Export Selection
        // Primary right-side anchor — "[N] ARMED" in Neon Gold, bold, slightly
        // larger so it carries visual weight as the main vault counter.
        selectionCountLabel_.setText ("[0] ARMED", juce::dontSendNotification);
        selectionCountLabel_.setColour (juce::Label::textColourId, pal::NeonGold);
        selectionCountLabel_.setJustificationType (juce::Justification::centredRight);
        selectionCountLabel_.setFont (juce::Font (juce::FontOptions { 12.5f })
                                          .boldened()
                                          .withExtraKerningFactor (0.12f));
        selectionCountLabel_.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (selectionCountLabel_);

        // Status — empty by default. The drop-zone panel is the empty-state cue;
        // this label only speaks during/after analysis (e.g. "Loaded 3 files").
        statusLabel_.setText ("");
        statusLabel_.setColour (juce::Colour (0xFF9A9A9A));
        statusLabel_.setFont (juce::Font (juce::FontOptions { 12.0f }));
        statusLabel_.setJustification (juce::Justification::centredRight);
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

        // exclusive=true: each vault tile click stops any currently-playing
        // voice so previews replace one another instead of layering/accumulating.
        resultsVault_->onTilePlay = [this] (auto f, auto s, auto e)
        {
            previewGrid_->playSlice (std::move (f), s, e, true);
        };
        resultsVault_->onTileSelected = [this] (auto f, auto s, auto e)
        {
            previewGrid_->playSlice (std::move (f), s, e, true);
        };
        resultsVault_->onExportCollection = [this]
        {
            produceAllSlices();
        };
        resultsVault_->onSelectionChanged = [this]
        {
            updateSelectionCount();
        };
        resultsVault_->onTileLanded = [this] (ResultTile& t)
        {
            queueTilePreRender (t);
        };

        // "Trash Compactor" — clear every tile in the vault (source cards stay).
        resultsVault_->onClearRequested = [this]
        {
            resultsVault_->clear();
            updateSelectionCount();
            setStatus ("Vault cleared.");
        };

        resultsVault_->onTileExternalDrag = [this] (ResultTile& tile)
        {
            // Bundled drag if the initiating tile is part of a Neon-Gold
            // multi-selection, otherwise just the initiating tile. Each tile
            // resolves to its pre-rendered temp WAV when one is available
            // (queueTilePreRender ran on the bg pool the moment the tile
            // landed); falls back to a synchronous render only when the bg
            // job hasn't completed yet (very fast clicks, or the pool is
            // backed up).
            const bool useSelection = tile.isMultiSelected()
                                   && resultsVault_->selectedTileCount() > 1;

            juce::StringArray paths;

            auto resolveOne = [&] (const ResultTile& t)
            {
                const auto pre = t.preRenderedFile();
                if (pre.existsAsFile())
                {
                    paths.add (pre.getFullPathName());
                    return;
                }

                // Fallback path — bg render hasn't landed yet. Same logic the
                // old sync-only code used; happens rarely now.
                if (! t.file()) return;
                const auto& tf = *t.file();
                const juce::int64 len = tf.samples.getNumSamples();
                const juce::int64 end = std::min (t.endSample(), len);
                if (end <= t.startSample()) return;

                const auto named = buildTileFilename (t);
                const juce::String fname = named.filename.isNotEmpty()
                    ? named.filename
                    : ("sb_drag_" + juce::String (t.sliceIndex()) + ".wav");

                auto tmp = tempDragDir().getChildFile (fname);
                if (tmp.existsAsFile())
                    tmp = tmp.getNonexistentSibling (false);
                renderSliceToWav (tf, t.startSample(), end, tmp, named.pitchHz);
                if (tmp.existsAsFile())
                    paths.add (tmp.getFullPathName());
            };

            if (useSelection)
                resultsVault_->forEachSelectedTile (
                    [&] (const ResultTile& t) { resolveOne (t); });
            else
                resolveOne (tile);

            if (paths.isEmpty()) return;

            // No post-drop delete: pre-rendered files persist for re-drag
            // within the same session. Startup janitor sweeps stale files on
            // next launch, so disk usage stays bounded across sessions.
            //
            // The drag call blocks on Windows but returns immediately on
            // macOS/Linux, so the guard flag must be cleared from the
            // completion callback — not on the next line — or a drop landing
            // back over this window would be re-ingested as a source card.
            performingDragOut_ = true;
            const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles (
                paths, false, &tile, [this] { performingDragOut_ = false; });
            if (! started)
                performingDragOut_ = false;
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
        // Stop any in-flight background export before children die: the job
        // checks this flag between slices and bails out promptly.
        exportCancel_->store (true);
        exportPool_.removeAllJobs (true, 4000);

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

        // Top bar — 80px tall; 10px vertical padding, 20px L/R outer padding so
        // the branding and counter don't choke against the window edge.
        auto bar = area.removeFromTop (kTopBarH);
        bar.reduce (kHeaderPadX, 10);

        // Branding pod is painted in paint() — reserve its full width here,
        // then a 15px breathing gap separates it from the first centre control.
        bar.removeFromLeft (kBrandWidth);
        bar.removeFromLeft (15);

        auto b32 = [] (juce::Rectangle<int> r)
        {
            return r.withSizeKeepingCentre (r.getWidth(), 32);
        };

        // Preferred control widths. When the bar can't fit them all (small
        // windows, or Grid mode's extra fields) every pod width scales down
        // uniformly instead of the rightmost controls collapsing to zero.
        constexpr int kBtnW          = 120;
        constexpr int kBtnExportW    = 150;
        constexpr int kBtnGap        = 10;
        constexpr int kCounterW      = 96;
        constexpr int kComboW   = 110;
        constexpr int kCenterG  = 15;
        constexpr int kSensW    = 68;   // wide enough for "1/32T" / "1/16T" in Grid mode
        constexpr int kSensGap  = 4;
        constexpr int kSliderW  = 150;
        constexpr int kBpmW     = 70;   // "AUTO" / "118 BPM"
        constexpr int kMaxW     = 56;   // "ALL" / "4"

        const int kBpmSlot = sliderInGridMode_
            ? (kSensGap + kBpmW + kSensGap + kMaxW) : 0;
        const int kCenterTotal =
            kComboW + kCenterG + kSensW + kSensGap + kSliderW + kBpmSlot;
        const int kRightTotal =
            kCounterW + kBtnGap + kBtnExportW + kBtnGap + kBtnW + kBtnGap + kBtnW;

        const float squeeze = juce::jlimit (0.55f, 1.0f,
            static_cast<float> (bar.getWidth())
            / static_cast<float> (kRightTotal + kCenterTotal + 2 * kBtnGap));
        const auto sq = [squeeze] (int w)
        {
            return juce::roundToInt (static_cast<float> (w) * squeeze);
        };

        // ── Right pod (Actions) — anchored to right edge ─────────────────────
        selectionCountLabel_.setBounds (bar.removeFromRight (sq (kCounterW)));
        bar.removeFromRight (sq (kBtnGap));
        exportSelectionBtn_.setBounds  (b32 (bar.removeFromRight (sq (kBtnExportW))));
        bar.removeFromRight (sq (kBtnGap));
        produceBtn_.setBounds          (b32 (bar.removeFromRight (sq (kBtnW))));
        bar.removeFromRight (sq (kBtnGap));
        outputDirBtn_.setBounds        (b32 (bar.removeFromRight (sq (kBtnW))));

        // ── Centre pod (Controls) — horizontally centred in remaining bar ─────
        const int centerTotalSq = sq (kCenterTotal);
        const int leftPad = juce::jmax (10, (bar.getWidth() - centerTotalSq) / 2);
        bar.removeFromLeft (leftPad);

        modeCombo_.setBounds        (b32 (bar.removeFromLeft (sq (kComboW))));
        bar.removeFromLeft (sq (kCenterG));
        sensitivityLabel_.setBounds (bar.removeFromLeft (sq (kSensW)));
        bar.removeFromLeft (kSensGap);
        sensitivitySlider_.setBounds(b32 (bar.removeFromLeft (sq (kSliderW))));
        if (sliderInGridMode_)
        {
            bar.removeFromLeft (kSensGap);
            bpmField_.setBounds (b32 (bar.removeFromLeft (sq (kBpmW))));
            bar.removeFromLeft (kSensGap);
            maxSamplesField_.setBounds (b32 (bar.removeFromLeft (sq (kMaxW))));
        }

        // Status fills whatever remains between centre group and right pod —
        // empty by default, populated by setStatus() during analysis.
        statusLabel_.setBounds (bar);

        // Body split — right panel: PreviewGrid (top) + ResultsVault (rest).
        // The grid gives ground on short windows so the vault always keeps at
        // least ~55% of the panel instead of being squeezed to a sliver.
        const int gridW = area.getWidth() * kGridFrac / 100;
        auto rightPanel = area.removeFromRight (gridW);

        const int padGridH = juce::jmin (kPreviewGridH,
                                         rightPanel.getHeight() * 45 / 100);
        previewGrid_->setBounds (rightPanel.removeFromTop (padGridH).reduced (8));

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

            // Logo — 50px, centred vertically in the 80px bar, anchored at the
            // outer 20px header pad so logo + wordmark feel like one cohesive unit.
            constexpr float kLogoPadX = static_cast<float> (kHeaderPadX);
            constexpr float kLogoSize = 50.0f;
            constexpr float kLogoToText = 15.0f;  // fixed gap, logo → wordmark
            const float kLogoPadY = (static_cast<float> (kTopBarH) - kLogoSize) * 0.5f;
            const juce::Rectangle<int> logoRect (
                static_cast<int> (kLogoPadX),
                static_cast<int> (kLogoPadY),
                static_cast<int> (kLogoSize),
                static_cast<int> (kLogoSize));

            // Neon cyan DropShadow glow radiating behind the logo
            {
                juce::Path glowShape;
                glowShape.addEllipse (logoRect.toFloat().reduced (4.0f));
                juce::DropShadow (juce::Colour (0xFF00FBFF).withAlpha (0.55f), 16, { 0, 0 })
                    .drawForPath (g, glowShape);
            }

            // Draw the PNG directly at high quality — alpha blends into dark header
            if (! logo.isNull())
            {
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImageWithin (logo, logoRect.getX(), logoRect.getY(),
                                   logoRect.getWidth(), logoRect.getHeight(),
                                   juce::RectanglePlacement::centred);
                g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
            }

            // Wordmark — vertically centred on the logo's mid-point.
            // Wordmark group has total height ~36 (12 "THE" + 4 lead + 20 "SWITCHBLADE").
            // To centre on the bar, top = (barH - 36) / 2.
            const float barHf     = static_cast<float> (kTopBarH);
            const float textX     = kLogoPadX + kLogoSize + kLogoToText;
            const float wmTotalH  = 36.0f;
            const float wmTop     = (barHf - wmTotalH) * 0.5f;
            const float lineTheH  = 12.0f;
            const float lineSwH   = 20.0f;
            const float lineLead  = 4.0f;
            const float wmRightX  = kHeaderPadX + kBrandWidth - 4.0f;
            const float wmW       = wmRightX - textX;

            // Line 1: "THE" — small, all-caps, wide-tracked, cyan accent
            {
                g.setFont (juce::Font (juce::FontOptions { 8.5f })
                               .boldened()
                               .withExtraKerningFactor (0.40f));
                g.setColour (pal::NeonCyan.withAlpha (0.55f));
                g.drawText ("THE",
                            juce::Rectangle<float> (textX, wmTop, wmW, lineTheH).toNearestInt(),
                            juce::Justification::centredLeft, false);
            }

            // Line 2: "SWITCHBLADE" — 14pt boldened with wide letterspacing
            {
                g.setFont (juce::Font (juce::FontOptions { 14.0f })
                               .boldened()
                               .withExtraKerningFactor (0.25f));

                const auto nameR = juce::Rectangle<float> (
                    textX, wmTop + lineTheH + lineLead,
                    wmW, lineSwH);

                // Subtle cyan shadow 1px below for depth
                g.setColour (pal::NeonCyan.withAlpha (0.20f));
                g.drawText ("SWITCHBLADE",
                            nameR.translated (0.0f, 1.0f).toNearestInt(),
                            juce::Justification::centredLeft, false);

                // Primary near-white
                g.setColour (juce::Colour (0xFFF0F4FF));
                g.drawText ("SWITCHBLADE", nameR.toNearestInt(),
                            juce::Justification::centredLeft, false);
            }

            // Vertical separator between brand pod and centre pod
            const float sepX = static_cast<float> (kHeaderPadX + kBrandWidth) + 6.0f;
            g.setColour (pal::NeonCyan.withAlpha (0.15f));
            g.drawVerticalLine (static_cast<int> (sepX), barHf * 0.20f, barHf * 0.80f);
            g.setColour (pal::ChromeMid.withAlpha (0.25f));
            g.drawVerticalLine (static_cast<int> (sepX) + 1, barHf * 0.20f, barHf * 0.80f);
        }

        // Header bottom border — solid #00FBFF 1px neon line + hard dark edge
        g.setColour (juce::Colour (0xFF00FBFF));
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
        {
            const juce::File jf (f);
            if (jf.isDirectory()) return true;          // batch: accept folders
            if (formatManager_.findFormatForFileExtension (jf.getFileExtension()))
                return true;
        }
        return false;
    }

    void MainContainer::filesDropped (const juce::StringArray& files, int, int)
    {
        dropHighlight_ = false;
        repaint();

        // Ignore our own drag-out round-tripping back onto the window: dragging
        // a vault tile out to the OS can end over this same window, and JUCE
        // then delivers it here as a drop — which would re-ingest the exported
        // slice as a phantom source card.
        if (performingDragOut_)
            return;

        // Expand folders to their audio-file children (recursive). Capped so
        // a casual drop of a deep tree — or a directory containing a Windows
        // reparse point / junction loop — can't freeze the UI for minutes or
        // blow out memory. Files beyond the cap are silently dropped and the
        // user sees a status message; they can re-drop a narrower selection.
        constexpr int kMaxBatchFiles = 500;
        juce::StringArray expanded;
        bool              capped = false;
        for (auto& f : files)
        {
            if (expanded.size() >= kMaxBatchFiles) { capped = true; break; }
            const juce::File jf (f);
            if (jf.isDirectory())
            {
                const auto wildcard = formatManager_.getWildcardForAllFormats();
                for (const auto& child : jf.findChildFiles (
                         juce::File::findFiles, true, wildcard))
                {
                    if (expanded.size() >= kMaxBatchFiles) { capped = true; break; }
                    expanded.add (child.getFullPathName());
                }
            }
            else
            {
                expanded.add (f);
            }
        }
        if (capped)
            setStatus ("Capped at " + juce::String (kMaxBatchFiles)
                       + " files (folder contained more).");

        const auto mode = currentMode();
        int queued = 0;

        for (auto& f : expanded)
        {
            const juce::File jf (f);
            if (! jf.existsAsFile()) continue;
            // Never re-ingest our own pre-rendered drag-out temp files (belt-
            // and-suspenders alongside the performingDragOut_ guard above).
            if (jf.getParentDirectory() == tempDragDir()) continue;

            // On Windows, std::filesystem::path(const std::string&) interprets
            // the bytes as the active ANSI codepage and corrupts UTF-8 paths
            // (juce::String::toStdString() returns UTF-8). Build from the
            // wide-char form so non-ASCII filenames survive end-to-end into
            // bext metadata and audio I/O.
           #if JUCE_WINDOWS
            const auto path = std::filesystem::path (
                jf.getFullPathName().toWideCharPointer());
           #else
            const auto path = std::filesystem::path (
                jf.getFullPathName().toStdString());
           #endif

            // Create an immediate pending card so the user sees activity at once
            auto card = std::make_unique<SampleCard> (formatManager_, thumbnailCache_);
            card->setDisplayPath (path);
            card->setLoading (false);   // onStarted will flip this
            card->onSelected = [this, rawCard = card.get()] { selectCard (rawCard); };
            card->onMultiSelectChanged = [this] { updateSelectionCount(); };
            card->onModeChangeRequested = [this, rawCard = card.get()]
                (switchblade::analysis::AnalysisMode m) { reAnalyzeCard (rawCard, m); };
            card->onContextMenuRequested = [this, rawCard = card.get()]
                { showCardContextMenu (rawCard); };
            card->onDragOut = [this, rawCard = card.get()] { dragOutCard (rawCard); };
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
            updateActionButtonStates();
            const int totalPending = static_cast<int> (pendingCards_.size());
            produceBtn_.setButtonText (
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
            ++batchFailures_;
            setStatus ("Error: " + result.errorMessage);
            if (card)
                card->setFailed (result.errorMessage);
            // Still update the button count — this job is now done
            if (analyzing_ && ! pendingCards_.empty())
                produceBtn_.setButtonText (
                    juce::String (juce::CharPointer_UTF8 ("ANALYZING\xe2\x80\xa6 ("))
                    + juce::String (pendingCards_.size()) + ")");
            return;
        }

        // Update live count on button
        if (analyzing_ && ! pendingCards_.empty())
            produceBtn_.setButtonText (
                juce::String (juce::CharPointer_UTF8 ("ANALYZING\xe2\x80\xa6 ("))
                + juce::String (pendingCards_.size()) + ")");

        // The job hands back the audio it analysed — no message-thread disk
        // re-read (which used to visibly stall the UI on long files).
        auto sharedFile = result.audio;
        if (! sharedFile)
        {
            ++batchFailures_;
            setStatus ("Load failed for: "
                       + juce::String (result.path.filename().string()));
            if (card)
                card->setFailed ("Could not load audio");
            return;
        }

        // If no pending card existed (e.g. engine was used directly), create one now
        if (card == nullptr)
        {
            auto newCard = std::make_unique<SampleCard> (formatManager_, thumbnailCache_);
            card = newCard.get();
            cardList_->addCard (card);
            cards_.push_back (std::move (newCard));
            card->onSelected = [this, card] { selectCard (card); };
            card->onMultiSelectChanged = [this] { updateSelectionCount(); };
            card->onContextMenuRequested = [this, card] { showCardContextMenu (card); };
            card->onDragOut = [this, card] { dragOutCard (card); };
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

        // Grid mode reports the beat period it used — surface it in the BPM
        // field so the user sees what was detected (and can override it).
        if (result.detectedBpm.has_value())
            showDetectedBpm (*result.detectedBpm);

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
            syncVaultForCard (*card);
        };

        card->onMarkerMoved = [this, card] (int, juce::int64)
        {
            // Per spec: a marker edit must immediately re-render the
            // corresponding slices in the result tiles. Boundaries are
            // re-finalised from the new marker positions, then only THIS
            // file's tiles are replaced — other tiles (and any multi-
            // selection) survive untouched.
            if (card->file())
            {
                auto ts = card->transients();
                switchblade::analysis::finalizeSliceBoundaries (*card->file(), ts);
                card->setTransients (std::move (ts));
            }
            refreshPreviewGrid();
            syncVaultForCard (*card);
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
            ? (juce::String::fromUTF8 (" \xe2\x80\xa2 ") + juce::String (static_cast<int> (normTargetDb_)) + "dB")
            : juce::String{};
        produceBtn_.setButtonText         ("Produce" + suffix);
        exportSelectionBtn_.setButtonText ("Export Selection" + suffix);
    }

    void MainContainer::updateOutputDirLabel() noexcept
    {
        if (outputDir_.isDirectory())
        {
            outputDirBtn_.setButtonText (outputDir_.getFileName());
            outputDirBtn_.setTooltip (outputDir_.getFullPathName()
                + "\nRight-click to reset to source folder.");
        }
        else
        {
            outputDirBtn_.setButtonText ("Source folder");
            outputDirBtn_.setTooltip ("Slices are written next to the source file.\n"
                                      "Click to choose a custom output folder.\n"
                                      "Right-click to reset.");
        }
    }

    void MainContainer::chooseOutputDir()
    {
        const auto startDir = outputDir_.isDirectory()
            ? outputDir_
            : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        fileChooser_ = std::make_unique<juce::FileChooser> (
            "Choose output folder", startDir, "");

        fileChooser_->launchAsync (
            juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                const auto results = fc.getResults();
                if (! results.isEmpty())
                {
                    outputDir_ = results[0];
                    updateOutputDirLabel();
                }
            });
    }

    //==========================================================================
    //  Per-card re-analysis (called from badge dropdown on message thread)
    //==========================================================================
    void MainContainer::reAnalyzeCard (SampleCard* card,
                                        switchblade::analysis::AnalysisMode mode)
    {
        if (card == nullptr) return;

        // Failed cards never received a file — fall back to the display path
        // so the badge dropdown doubles as their retry control.
        const auto path = card->file() ? card->file()->path
                                       : card->displayPath();
        if (path.empty()) return;

        // Remove any stale pending-job entry pointing at this card.
        for (auto it = pendingCards_.begin(); it != pendingCards_.end(); )
        {
            if (it->second == card) it = pendingCards_.erase (it);
            else ++it;
        }

        card->setLoading (true);
        const int jobId = engine_.enqueue (path, mode);
        pendingCards_.emplace (jobId, card);

        analyzing_ = true;
        updateActionButtonStates();
        produceBtn_.setButtonText (
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
        updateNormLabel();              // restores "Produce" (+ any norm suffix)
        updateActionButtonStates();

        // Rebuild vault in cards_ (drop) order so tile indices always match
        // grid pad indices: tile 001 = pad 1 (key "1"), tile 016 = pad V (key "V").
        if (resultsVault_)
        {
            resultsVault_->clear();
            for (const auto& card : cards_)
            {
                if (! card->file() || card->transients().empty()) continue;

                juce::String noteName;
                if (wantsPitch (card->classification())
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

        // Failures would otherwise be invisible: each error status line is
        // overwritten by the next completion, so the batch summary is the one
        // place a "3 failed" can reliably reach the user.
        const juce::String failNote = batchFailures_ > 0
            ? (juce::String (juce::CharPointer_UTF8 ("  \xe2\x80\x94  "))
               + juce::String (batchFailures_) + " FAILED")
            : juce::String{};
        batchFailures_ = 0;

        setStatus (juce::String (totalCards)  + " file"
                   + (totalCards  != 1 ? "s" : "")
                   + juce::String (juce::CharPointer_UTF8 ("  \xe2\x80\x94  "))
                   + juce::String (totalSlices) + " slice"
                   + (totalSlices != 1 ? "s" : "") + " extracted"
                   + failNote);
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
    //  Drag a card's source file out to the OS / DAW
    //==========================================================================
    void MainContainer::dragOutCard (SampleCard* card)
    {
        if (card == nullptr || ! card->file()) return;

        const auto path = juce::String (card->file()->path.string());
        if (path.isEmpty()) return;

        juce::File f (path);
        if (! f.existsAsFile()) return;   // file moved or deleted under us

        juce::StringArray paths;
        paths.add (f.getFullPathName());

        // canMoveFiles = false: never let a DAW take ownership of the user's
        // original source. The OS does a copy-on-import for safety. The guard
        // flag is cleared via the completion callback because the call is
        // asynchronous on macOS/Linux (see onTileExternalDrag).
        performingDragOut_ = true;
        const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles (
            paths, false, card, [this] { performingDragOut_ = false; });
        if (! started)
            performingDragOut_ = false;
    }

    //==========================================================================
    //  Background drag pre-render
    //==========================================================================
    void MainContainer::queueTilePreRender (ResultTile& tile)
    {
        // Snapshot every value the bg job needs so nothing reaches across
        // threads back to the UI. file is a shared_ptr<const AudioFile> — safe
        // to read concurrently. The SafePointer is the only handle that
        // touches the tile after dispatch, and it's checked on the message
        // thread before being deref'd.
        const auto file = tile.file();
        if (! file) return;
        const juce::int64 start = tile.startSample();
        const juce::int64 end   = std::min (tile.endSample(),
                                            static_cast<juce::int64> (
                                                file->samples.getNumSamples()));
        if (end <= start) return;

        const auto                                classification = tile.classification();
        const int                                 sliceIndex     = tile.sliceIndex();
        const juce::String                        fileWideNote   = tile.noteName();
        juce::Component::SafePointer<ResultTile>  weak           = &tile;
        const juce::File                          tempDir        = tempDragDir();
        const float                               normDb         = normTargetDb_;

        dragRenderPool_.addJob ([file, start, end, classification, sliceIndex,
                                 fileWideNote, weak, tempDir, normDb] () mutable
        {
            // Mirror buildTileFilename's logic for the on-disk filename so the
            // DAW sees the same meaningful name (e.g. Bass_E1+12c_03.wav) the
            // sync render path produced before this background pass.
            const auto& tf = *file;
            const auto  stem = juce::File (juce::String (tf.path.string()))
                                   .getFileNameWithoutExtension();
            const auto  tag  = classificationTag (classification);

            juce::String         keySuffix;
            std::optional<float> pitchHz;
            if (switchblade::analysis::wantsPitch (classification))
            {
                pitchHz = switchblade::analysis::detectSlicePitchHz (tf, start, end);
                if (pitchHz.has_value())
                    keySuffix = juce::String (
                        switchblade::analysis::PitchDetector::noteNameWithCentsFromHz (*pitchHz));
                else
                    keySuffix = fileWideNote;
            }

            const auto fname = makeSliceFilename (stem, tag, keySuffix, sliceIndex);
            if (fname.isEmpty()) return;

            auto outFile = tempDir.getChildFile (fname);
            if (outFile.existsAsFile())
                outFile = outFile.getNonexistentSibling (false);

            renderSliceToWavImpl (tf, start, end, outFile, pitchHz, normDb);
            if (! outFile.existsAsFile()) return;

            juce::MessageManager::callAsync ([weak, outFile] () mutable
            {
                if (auto* t = weak.getComponent())
                    t->setPreRenderedFile (std::move (outFile));
            });
        });
    }

    //==========================================================================
    //  Card deletion
    //==========================================================================
    juce::PropertiesFile& MainContainer::userSettings()
    {
        if (! userSettings_)
        {
            juce::PropertiesFile::Options opts;
            opts.applicationName     = "Switchblade";
            opts.filenameSuffix      = ".settings";
            opts.osxLibrarySubFolder = "Application Support";
            opts.commonToAllUsers    = false;
            opts.storageFormat       = juce::PropertiesFile::storeAsXML;
            userSettings_ = std::make_unique<juce::PropertiesFile> (opts);
        }
        return *userSettings_;
    }

    bool MainContainer::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
        {
            // Build target list from the multi-selection; fall back to the
            // single highlighted card if nothing is multi-selected.
            std::vector<SampleCard*> targets;
            for (auto& c : cards_)
                if (c && c->isMultiSelected()) targets.push_back (c.get());
            if (targets.empty() && selectedCard_)
                targets.push_back (selectedCard_);

            if (! targets.empty())
            {
                requestDeleteCards (std::move (targets));
                return true;
            }
            return false;
        }

        // Space — preview the highlighted card (replaces any playing voice).
        if (k == juce::KeyPress (juce::KeyPress::spaceKey))
        {
            if (selectedCard_ != nullptr && selectedCard_->file())
            {
                const auto f = selectedCard_->file();
                previewGrid_->playSlice (
                    f, 0,
                    static_cast<juce::int64> (f->samples.getNumSamples()),
                    true);
                return true;
            }
            return false;
        }

        // Esc — stop playback and clear every selection.
        if (k == juce::KeyPress (juce::KeyPress::escapeKey))
        {
            previewGrid_->stopAll();
            if (resultsVault_)
                resultsVault_->setAllTilesSelected (false);
            for (auto& c : cards_)
                if (c) c->setMultiSelected (false);
            updateSelectionCount();
            return true;
        }

        // Cmd/Ctrl+A — arm every vault tile for Export Selection.
        if (k.getKeyCode() == 'A' && k.getModifiers().isCommandDown())
        {
            if (resultsVault_ && resultsVault_->tileCount() > 0)
            {
                resultsVault_->setAllTilesSelected (true);
                updateSelectionCount();
                return true;
            }
            return false;
        }

        // Up / Down — walk the card list, keeping the selection in view.
        if (k == juce::KeyPress (juce::KeyPress::upKey)
            || k == juce::KeyPress (juce::KeyPress::downKey))
        {
            if (cards_.empty())
                return false;

            const int step = (k == juce::KeyPress (juce::KeyPress::downKey)) ? 1 : -1;
            int idx = -1;
            for (std::size_t i = 0; i < cards_.size(); ++i)
                if (cards_[i].get() == selectedCard_) { idx = static_cast<int> (i); break; }

            const int last = static_cast<int> (cards_.size()) - 1;
            const int next = (idx < 0) ? (step > 0 ? 0 : last)
                                       : juce::jlimit (0, last, idx + step);
            auto* target = cards_[static_cast<std::size_t> (next)].get();
            selectCard (target);

            // Scroll the viewport just enough to reveal the new selection.
            const auto b    = target->getBounds();       // in cardList_ coords
            const auto view = cardViewport_.getViewArea();
            if (b.getY() < view.getY())
                cardViewport_.setViewPosition (
                    0, juce::jmax (0, b.getY() - CardListComponent::kCardGap));
            else if (b.getBottom() > view.getBottom())
                cardViewport_.setViewPosition (
                    0, b.getBottom() + CardListComponent::kCardGap
                       - cardViewport_.getHeight());
            return true;
        }

        return false;
    }

    void MainContainer::showCardContextMenu (SampleCard* clickedCard)
    {
        if (clickedCard == nullptr) return;

        // If the clicked card is part of the multi-selection, the menu acts
        // on the whole set; otherwise it acts on just this one card.
        std::vector<SampleCard*> targets;
        for (auto& c : cards_)
            if (c && c->isMultiSelected()) targets.push_back (c.get());

        const bool clickedIsMulti = std::find (targets.begin(), targets.end(), clickedCard)
                                  != targets.end();
        if (! clickedIsMulti || targets.size() < 2)
            targets = { clickedCard };

        const int n = static_cast<int> (targets.size());
        juce::PopupMenu menu;
        menu.addItem (1, n == 1 ? juce::String ("Delete card")
                                : "Delete " + juce::String (n) + " selected cards");

        // Escape hatch for the "Don't ask me again" checkbox — without this
        // the remembered delete behaviour is a permanent, invisible trap.
        const bool hasRememberedChoice =
            userSettings().getValue ("deleteCardsAction", "prompt") != "prompt";
        if (hasRememberedChoice)
            menu.addItem (2, "Ask again before deleting cards with slices");

        menu.showMenuAsync (
            juce::PopupMenu::Options{}.withTargetComponent (clickedCard),
            [this, targets = std::move (targets)] (int result) mutable
            {
                if (result == 1)
                {
                    requestDeleteCards (std::move (targets));
                }
                else if (result == 2)
                {
                    userSettings().setValue ("deleteCardsAction", "prompt");
                    userSettings().saveIfNeeded();
                    setStatus ("Delete confirmation re-enabled.");
                }
            });
    }

    void MainContainer::requestDeleteCards (std::vector<SampleCard*> targets)
    {
        if (targets.empty()) return;

        // Count vault slices that came from these cards. If none, just delete
        // silently — there's nothing the user could lose by skipping the prompt.
        int sliceCount = 0;
        for (auto* c : targets)
            if (c && c->file())
                sliceCount += resultsVault_->countSlicesForFile (c->file());

        if (sliceCount == 0)
        {
            deleteCards (targets, /*alsoDeleteSlices=*/ false);
            return;
        }

        // Persisted "Don't ask me again" preference takes the prompt out of the loop.
        const juce::String pref = userSettings().getValue ("deleteCardsAction", "prompt");
        if (pref == "deleteAll")  { deleteCards (targets, true);  return; }
        if (pref == "keepSlices") { deleteCards (targets, false); return; }

        // Build the three-button modal with a "Don't ask me again" toggle.
        // The AlertWindow and ToggleButton are held as members (same pattern
        // as fileChooser_) so they survive across the async callback.
        const juce::String title = targets.size() == 1
            ? juce::String ("Delete card?")
            : "Delete " + juce::String (targets.size()) + " cards?";
        const juce::String msg =
            juce::String (sliceCount) + " generated slice(s) in the Vault came from "
            + (targets.size() == 1 ? juce::String ("this card.")
                                   : juce::String ("these cards."))
            + "\n\nWhat should happen to them?";

        deletePrompt_ = std::make_unique<juce::AlertWindow> (
            title, msg, juce::MessageBoxIconType::QuestionIcon);

        deletePromptRemember_ = std::make_unique<SimpleCheckbox> ("Don't ask me again");
        deletePromptRemember_->setSize (170, 20);
        deletePrompt_->addCustomComponent (deletePromptRemember_.get());

        deletePrompt_->addButton ("Delete cards + slices", 1,
                                  juce::KeyPress (juce::KeyPress::returnKey));
        deletePrompt_->addButton ("Delete cards only",     2);
        deletePrompt_->addButton ("Cancel",                0,
                                  juce::KeyPress (juce::KeyPress::escapeKey));

        deletePrompt_->enterModalState (true,
            juce::ModalCallbackFunction::create (
                [this, targets = std::move (targets)] (int result) mutable
                {
                    const bool persist = deletePromptRemember_
                                       && deletePromptRemember_->getToggleState();
                    // Tear down the modal owners *before* mutating state, so a
                    // re-entrant delete can spin up a fresh prompt cleanly.
                    deletePromptRemember_.reset();
                    deletePrompt_.reset();

                    if (result == 0) return;   // Cancel

                    const bool deleteSlices = (result == 1);
                    if (persist)
                    {
                        userSettings().setValue ("deleteCardsAction",
                            deleteSlices ? "deleteAll" : "keepSlices");
                        userSettings().saveIfNeeded();
                    }
                    deleteCards (targets, deleteSlices);
                }),
            false);
    }

    void MainContainer::deleteCards (const std::vector<SampleCard*>& targets,
                                     bool alsoDeleteSlices)
    {
        if (targets.empty()) return;

        for (auto* target : targets)
        {
            if (target == nullptr) continue;

            // Drop any in-flight analysis job pointing at this card so its
            // result doesn't try to populate a destroyed component.
            for (auto it = pendingCards_.begin(); it != pendingCards_.end(); )
            {
                if (it->second == target) it = pendingCards_.erase (it);
                else                      ++it;
            }

            // Vault tiles, optionally.
            if (alsoDeleteSlices && target->file())
                resultsVault_->removeSlicesForFile (target->file());

            // Clear selection if this was the previewed card.
            if (selectedCard_ == target)
                selectedCard_ = nullptr;

            cardList_->removeCard (target);

            cards_.erase (
                std::remove_if (cards_.begin(), cards_.end(),
                                [target] (const std::unique_ptr<SampleCard>& p)
                                { return p.get() == target; }),
                cards_.end());
        }

        // Re-elect a selected card if we just orphaned the preview grid.
        if (selectedCard_ == nullptr && ! cards_.empty())
            selectCard (cards_.front().get());
        else if (cards_.empty())
            previewGrid_->clear();

        updateSelectionCount();
        repaint();
    }

    //==========================================================================
    //  Batch rendering — spec collection happens here on the message thread;
    //  pitch detection + WAV writes run on exportPool_ (see
    //  startBackgroundExport) so a big batch never freezes the UI.
    //==========================================================================
    namespace
    {
        // Slice boundary rule shared by every export path: energy-decay
        // natural end when set, else next onset, else file end.
        [[nodiscard]] juce::int64 sliceEndFor (
            const std::vector<switchblade::analysis::Transient>& ts,
            std::size_t i, juce::int64 len) noexcept
        {
            const juce::int64 end = (ts[i].naturalEnd > 0)
                ? ts[i].naturalEnd
                : ((i + 1 < ts.size()) ? ts[i + 1].sampleIndex : len);
            return std::min (end, len);
        }
    } // namespace

    void MainContainer::renderAndExportCard (SampleCard& card)
    {
        if (! card.file() || card.transients().empty()) return;

        const auto& tf = *card.file();
        const auto& ts = card.transients();
        const juce::int64 len = tf.samples.getNumSamples();
        const auto stem   = juce::File (juce::String (tf.path.string()))
                                .getFileNameWithoutExtension();
        const auto outDir = juce::File (juce::String (tf.path.parent_path().string()));
        const bool pitched = wantsPitch (card.classification());

        std::vector<ExportSpec> specs;
        specs.reserve (ts.size());
        for (std::size_t i = 0; i < ts.size(); ++i)
            specs.push_back ({ card.file(),
                               ts[i].sampleIndex, sliceEndFor (ts, i, len),
                               card.classification(), stem, {},
                               pitched ? card.pitchHz() : std::optional<float>{},
                               static_cast<int> (i + 1), outDir });

        startBackgroundExport (std::move (specs));
    }

    void MainContainer::produceAllSlices()
    {
        if (cards_.empty()) { setStatus ("No files loaded."); return; }

        // Selection-aware: if the user has Neon-Gold-selected any vault tiles,
        // Produce acts on that selection only. Sidebar card multi-select
        // doesn't count (per the spec — only vault tiles are eligible for
        // Export Selection).
        const bool anySelected =
            resultsVault_ && resultsVault_->selectedTileCount() > 0;
        if (anySelected) { exportSelection(); return; }

        std::vector<ExportSpec> specs;
        for (const auto& card : cards_)
        {
            if (! card->file() || card->transients().empty()) continue;
            const auto& tf = *card->file();
            const auto& ts = card->transients();
            const juce::int64 len = tf.samples.getNumSamples();
            const auto stem  = juce::File (juce::String (tf.path.string()))
                                   .getFileNameWithoutExtension();
            const auto outDir = outputDir_.isDirectory()
                ? outputDir_
                : juce::File (juce::String (tf.path.parent_path().string()));
            const bool pitched = wantsPitch (card->classification());

            for (std::size_t i = 0; i < ts.size(); ++i)
                specs.push_back ({ card->file(),
                                   ts[i].sampleIndex, sliceEndFor (ts, i, len),
                                   card->classification(), stem, {},
                                   pitched ? card->pitchHz() : std::optional<float>{},
                                   static_cast<int> (i + 1), outDir });
        }
        startBackgroundExport (std::move (specs));
    }

    void MainContainer::exportSelection()
    {
        // Per spec: only Neon-Gold vault tiles are eligible for Export
        // Selection. Sidebar SampleCard multi-select is a visual cue only
        // and does NOT contribute to the export — preventing accidental
        // bulk exports of an entire file when the user really meant to
        // grab a few specific slices from the result vault.
        std::vector<ExportSpec> specs;
        if (resultsVault_)
        {
            resultsVault_->forEachSelectedTile (
                [this, &specs] (const ResultTile& tile)
                {
                    const auto file = tile.file();
                    if (! file) return;

                    const juce::int64 len = file->samples.getNumSamples();
                    const auto stem = juce::File (juce::String (file->path.string()))
                                          .getFileNameWithoutExtension();
                    const auto outDir = outputDir_.isDirectory()
                        ? outputDir_
                        : juce::File (juce::String (
                              file->path.parent_path().string()));

                    specs.push_back ({ file,
                                       tile.startSample(),
                                       std::min (tile.endSample(), len),
                                       tile.classification(), stem,
                                       tile.noteName(), {},
                                       tile.sliceIndex(), outDir });
                });
        }

        if (specs.empty())
        {
            setStatus (juce::String ("Nothing selected. ") + kMultiSelectKeyName
                       + "+click vault tiles to select slices.");
            return;
        }
        startBackgroundExport (std::move (specs));
    }

    void MainContainer::startBackgroundExport (std::vector<ExportSpec> specs)
    {
        if (exporting_)
        {
            setStatus (juce::String (juce::CharPointer_UTF8 (
                "An export is already running\xe2\x80\xa6")));
            return;
        }
        if (specs.empty())
        {
            setStatus ("Nothing to export.");
            return;
        }

        exporting_ = true;
        updateActionButtonStates();
        exportCancel_->store (false);

        const auto total  = static_cast<int> (specs.size());
        const float normDb = normTargetDb_;
        auto cancel = exportCancel_;
        juce::Component::SafePointer<MainContainer> weak = this;

        setStatus (juce::String (juce::CharPointer_UTF8 ("Exporting\xe2\x80\xa6 0/"))
                   + juce::String (total));

        exportPool_.addJob ([specs = std::move (specs), total, normDb,
                             cancel, weak] () mutable
        {
            int done = 0;
            for (const auto& s : specs)
            {
                if (cancel->load()) return;
                if (! s.file || s.end <= s.start) { ++done; continue; }

                // Per-slice pitch (YIN) runs HERE, off the message thread —
                // it's the dominant cost for melodic/grid batches.
                std::optional<float> pitchHz;
                juce::String keySuffix;
                if (switchblade::analysis::wantsPitch (s.classification))
                {
                    pitchHz = detectSlicePitchHz (*s.file, s.start, s.end);
                    if (! pitchHz.has_value()) pitchHz = s.fallbackPitchHz;
                    if (pitchHz.has_value())
                        keySuffix = juce::String (
                            switchblade::analysis::PitchDetector::noteNameWithCentsFromHz (
                                *pitchHz));
                    else
                        keySuffix = s.fallbackNote;
                }

                const auto fname = makeSliceFilename (
                    s.stem, classificationTag (s.classification), keySuffix, s.index);
                renderSliceToWavImpl (*s.file, s.start, s.end,
                                      s.outDir.getChildFile (fname), pitchHz, normDb);
                ++done;

                juce::MessageManager::callAsync ([weak, done, total]
                {
                    if (auto* mc = weak.getComponent())
                        mc->setStatus (juce::String (juce::CharPointer_UTF8 (
                                           "Exporting\xe2\x80\xa6 "))
                                       + juce::String (done) + "/"
                                       + juce::String (total));
                });
            }

            juce::MessageManager::callAsync ([weak, total]
            {
                if (auto* mc = weak.getComponent())
                {
                    mc->exporting_ = false;
                    mc->updateActionButtonStates();
                    mc->setStatus (juce::String (total) + " slice"
                                   + (total != 1 ? "s" : "") + " exported.");
                }
            });
        });
    }

    void MainContainer::updateActionButtonStates()
    {
        // Exports read card transients and write files derived from them, so
        // both primary actions stay off while analysis is rewriting the model
        // or an export batch is already in flight.
        const bool busy = analyzing_ || exporting_;
        produceBtn_.setEnabled (! busy);
        exportSelectionBtn_.setEnabled (! busy);
    }

    void MainContainer::renderSliceToWav (
        const switchblade::analysis::AudioFile& file,
        juce::int64 start, juce::int64 end,
        const juce::File& outFile,
        std::optional<float> pitchHz) const
    {
        renderSliceToWavImpl (file, start, end, outFile, pitchHz, normTargetDb_);
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
        selectionCountLabel_.setText ("[" + juce::String (n) + "] ARMED",
                                      juce::dontSendNotification);
    }

    void MainContainer::syncVaultForCard (SampleCard& card)
    {
        if (! resultsVault_ || ! card.file()) return;

        juce::String noteName;
        if (wantsPitch (card.classification())
            && card.pitchHz().has_value()
            && card.pitchClarity().value_or (0.0f) > 0.5f)
        {
            noteName = juce::String (
                switchblade::analysis::PitchDetector::noteNameFromHz (
                    *card.pitchHz()));
        }
        resultsVault_->updateSlicesForFile (card.file(), card.transients(),
                                            card.classification(), noteName);
        // A replaced run may have carried selected tiles; keep the counter live.
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

    //==========================================================================
    //  Sensitivity / Division slider context switch
    //==========================================================================
    void MainContainer::applyModeSliderConfig()
    {
        const bool wantGrid =
            (currentMode() == switchblade::analysis::AnalysisMode::Grid);

        if (wantGrid == sliderInGridMode_)
            return;   // nothing changed

        sliderInGridMode_ = wantGrid;

        if (wantGrid)
        {
            // 10 musical subdivision stops (kGridStops): powers of 2 + triplets.
            // Log-skew the slider so each doubling occupies equal visual width
            // — the midpoint sits between 1/8 (8) and 1/8T (12), which is the
            // geometric mean sqrt(2 * 64) ≈ 11.31.
            sensitivitySlider_.setRange (2.0, 64.0, 1.0);
            sensitivitySlider_.setSkewFactorFromMidPoint (std::sqrt (2.0 * 64.0));
            sensitivitySlider_.setValue (16.0, juce::dontSendNotification);
            sensitivityLabel_.setText (gridStopLabel (16),
                                       juce::dontSendNotification);
            engine_.setGridDivisions (16);

            // Enter Grid: reset to auto-detect / keep-all and reveal the fields.
            bpmUserOverride_ = false;
            engine_.setManualBpm (0.0);
            bpmField_.setText ("AUTO", juce::dontSendNotification);
            bpmField_.setVisible (true);

            engine_.setGridMaxSamples (0);
            maxSamplesField_.setText ("ALL", juce::dontSendNotification);
            maxSamplesField_.setVisible (true);
        }
        else
        {
            sensitivitySlider_.setRange (0.3, 1.3, 0.01);
            sensitivitySlider_.setSkewFactor (1.0);   // restore linear
            sensitivitySlider_.setValue (1.0, juce::dontSendNotification);
            sensitivityLabel_.setText ("SENS", juce::dontSendNotification);
            engine_.setDetectorParams (buildDetectorParams());
            bpmField_.setVisible (false);
            maxSamplesField_.setVisible (false);
        }

        resized();   // re-lay the centre pod now that bpmField_ visibility changed
    }

    void MainContainer::showDetectedBpm (double bpm)
    {
        // The user's manual entry wins — never overwrite it with a detection.
        if (bpmUserOverride_ || bpm <= 0.0)
            return;
        bpmField_.setText (juce::String (juce::roundToInt (bpm)) + " BPM",
                           juce::dontSendNotification);
    }

    void MainContainer::commitBpmField()
    {
        // Parse the leading number; "AUTO", empty, or 0 returns to auto-detect.
        const double v = bpmField_.getText().trim().getDoubleValue();
        if (v > 0.0)
        {
            bpmUserOverride_ = true;
            engine_.setManualBpm (v);
            bpmField_.setText (juce::String (juce::roundToInt (v)) + " BPM",
                               juce::dontSendNotification);
        }
        else
        {
            bpmUserOverride_ = false;
            engine_.setManualBpm (0.0);
            bpmField_.setText ("AUTO", juce::dontSendNotification);
        }

        if (sliderInGridMode_)
            reAnalyzeLoadedCards();
    }

    void MainContainer::commitMaxSamplesField()
    {
        // Parse the leading integer; "ALL", empty, or 0 means keep every cell.
        const int n = maxSamplesField_.getText().trim().getIntValue();
        if (n > 0)
        {
            engine_.setGridMaxSamples (n);
            maxSamplesField_.setText (juce::String (n), juce::dontSendNotification);
        }
        else
        {
            engine_.setGridMaxSamples (0);
            maxSamplesField_.setText ("ALL", juce::dontSendNotification);
        }

        if (sliderInGridMode_)
            reAnalyzeLoadedCards();
    }

    void MainContainer::reAnalyzeLoadedCards()
    {
        // Re-slice every loaded card under the current mode — mode-combo
        // changes and Grid tweaks (subdivision / BPM / max samples) all take
        // effect on already-analysed files.
        const auto mode = currentMode();
        for (const auto& c : cards_)
            if (c && c->file())
                reAnalyzeCard (c.get(), mode);
    }

    switchblade::analysis::AnalysisMode MainContainer::currentMode() const noexcept
    {
        using M = switchblade::analysis::AnalysisMode;
        switch (modeCombo_.getSelectedId())
        {
            case 2:  return M::Percussive;
            case 3:  return M::Melodic;
            case 4:  return M::Texture;
            case 5:  return M::Grid;
            default: return M::Auto;
        }
    }

    float MainContainer::currentSensitivity() const noexcept
    {
        // Honour the documented [0.3, 1.3] range: when the slider has been
        // repurposed for Grid divisions (2..64) the raw value is meaningless
        // as a sensitivity. Return the neutral default so any caller — present
        // or future — gets a value inside the detector's design envelope.
        if (sliderInGridMode_)
            return 1.0f;
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
