#include "UI/ResultsVault.h"
#include "Core/Palette.h"
#include "Analysis/PitchDetector.h"

namespace switchblade::ui
{
    namespace pal = switchblade::palette;

    ResultsVault::ResultsVault (juce::AudioFormatManager& fmt,
                                juce::AudioThumbnailCache& cache)
        : fmt_   (fmt)
        , cache_ (cache)
    {
        setSize (300, kBadgeH + kGap);
    }

    ResultsVault::~ResultsVault()
    {
        stopTimer();
    }

    //==========================================================================
    //  Public API
    //==========================================================================
    void ResultsVault::setViewportWidth (int w)
    {
        viewW_ = std::max (kCols * 40, w);
        relayout();
    }

    void ResultsVault::addSlices (
        AudioFilePtr file,
        const std::vector<switchblade::analysis::Transient>& transients,
        switchblade::analysis::SourceClass classification,
        juce::String noteName)
    {
        if (! file || transients.empty()) return;

        const juce::int64 totalSamples =
            static_cast<juce::int64> (file->samples.getNumSamples());

        const bool isMelodic = classification == switchblade::analysis::SourceClass::Melodic;

        for (std::size_t i = 0; i < transients.size(); ++i)
        {
            const juce::int64 start = transients[i].sampleIndex;
            const juce::int64 end = (transients[i].naturalEnd > 0)
                ? transients[i].naturalEnd
                : ((i + 1 < transients.size())
                    ? transients[i + 1].sampleIndex
                    : totalSamples);
            const juce::int64 endC = std::min (end, totalSamples);

            // Per-slice pitch detection — each tile shows the note its own
            // audio actually contains, not a single file-wide pitch. Falls
            // back to the supplied file-wide noteName only if per-slice fails.
            juce::String thisNote = noteName;
            if (isMelodic)
            {
                if (auto hz = switchblade::analysis::detectSlicePitchHz (*file, start, endC))
                    thisNote = juce::String (
                        switchblade::analysis::PitchDetector::noteNameFromHz (*hz));
            }

            pending_.push_back ({ file, start, endC,
                                  classification, thisNote, nextTileIndex_++ });
        }

        if (! isTimerRunning())
            startTimer (kStaggerMs);
    }

    void ResultsVault::clear()
    {
        stopTimer();
        pending_.clear();
        for (auto& t : tiles_)
            removeChildComponent (t.get());
        tiles_.clear();
        nextTileIndex_ = 1;
        allDone_        = false;
        ceremonyPhase_  = 0.0f;
        relayout();
        repaint();
    }

    void ResultsVault::forEachSelectedTile (std::function<void (const ResultTile&)> fn) const
    {
        if (! fn) return;
        for (const auto& t : tiles_)
            if (t && t->isMultiSelected())
                fn (*t);
    }

    int ResultsVault::selectedTileCount() const noexcept
    {
        int n = 0;
        for (const auto& t : tiles_)
            if (t && t->isMultiSelected())
                ++n;
        return n;
    }

    void ResultsVault::setNormMode (bool active)
    {
        normMode_ = active;
        for (auto& t : tiles_)
            if (t) t->setNormalized (active);
        repaint();
    }

    void ResultsVault::triggerCompletionCeremony()
    {
        if (allDone_) return;           // idempotent
        allDone_       = true;
        ceremonyPhase_ = 0.0f;
        relayout();                     // grow height to accommodate export bar
        if (! isTimerRunning())
            startTimer (kAnimMs);
    }

    //==========================================================================
    //  Timer
    //==========================================================================
    void ResultsVault::timerCallback()
    {
        bool keepGoing = false;

        // ── 1. Tile stagger ────────────────────────────────────────────────────
        if (! pending_.empty())
        {
            dropOneTile();
            keepGoing = true;
        }

        // ── 2. Ceremony slide-in animation ─────────────────────────────────────
        if (allDone_ && ceremonyPhase_ < 1.0f)
        {
            // 350 ms total slide-in duration
            ceremonyPhase_ += static_cast<float> (kAnimMs) / 350.0f;
            if (ceremonyPhase_ >= 1.0f)
                ceremonyPhase_ = 1.0f;
            else
                keepGoing = true;

            relayout();
            repaint();
        }

        if (! keepGoing)
        {
            stopTimer();
            repaint();
        }
    }

    void ResultsVault::dropOneTile()
    {
        if (pending_.empty()) return;

        auto ps = std::move (pending_.front());
        pending_.pop_front();

        auto tile = std::make_unique<ResultTile> (
            fmt_, cache_,
            ps.file, ps.start, ps.end,
            ps.classification, ps.index, ps.noteName);

        tile->onPlay = [this] (auto f, auto s, auto e)
        {
            if (onTilePlay) onTilePlay (std::move (f), s, e);
        };
        tile->onSelected = [this] (auto f, auto s, auto e)
        {
            if (onTileSelected) onTileSelected (std::move (f), s, e);
        };
        tile->onMultiSelectChanged = [this]
        {
            if (onSelectionChanged) onSelectionChanged();
        };
        tile->onExternalDrag = [this] (ResultTile& t)
        {
            if (onTileExternalDrag) onTileExternalDrag (t);
        };

        tile->setNormalized (normMode_);
        addAndMakeVisible (*tile);
        tile->triggerEntryGlow();
        tiles_.push_back (std::move (tile));

        relayout();
        repaint();
    }

    //==========================================================================
    //  Layout
    //==========================================================================
    void ResultsVault::relayout()
    {
        const int tw      = tileW();
        const int n       = static_cast<int> (tiles_.size());
        const int yOrigin = kBadgeH + kGap;

        for (int i = 0; i < n; ++i)
        {
            const int col = i % kCols;
            const int row = i / kCols;
            tiles_[static_cast<std::size_t> (i)]->setBounds (
                kGap + col * (tw + kGap),
                yOrigin + row * (ResultTile::kTileH + kGap),
                tw, ResultTile::kTileH);
        }

        setSize (viewW_, computeHeight());
    }

    int ResultsVault::tileW() const noexcept
    {
        return std::max (20, (viewW_ - (kCols + 1) * kGap) / kCols);
    }

    int ResultsVault::computeHeight() const noexcept
    {
        int h;
        if (tiles_.empty())
            h = kBadgeH + kGap;
        else
        {
            const int rows = (static_cast<int> (tiles_.size()) + kCols - 1) / kCols;
            h = kBadgeH + kGap + rows * (ResultTile::kTileH + kGap) + kGap;
        }
        if (allDone_)
            h += kExportBarH;   // space for ceremony bar (slides in via paint offset)
        return h;
    }

    void ResultsVault::resized()
    {
        relayout();
    }

    void ResultsVault::mouseDown (const juce::MouseEvent& e)
    {
        if (! allDone_ || ceremonyPhase_ < 0.5f) return;

        // Hit-test the export bar (bottom kExportBarH px)
        const int barTop = getHeight() - kExportBarH;
        if (e.getPosition().getY() >= barTop)
        {
            if (onExportCollection)
                onExportCollection();
        }
    }

    //==========================================================================
    //  Paint
    //==========================================================================
    void ResultsVault::paint (juce::Graphics& g)
    {
        g.fillAll (pal::ChromeVoid);

        // Badge bar
        const auto badgeBar = juce::Rectangle<int> (0, 0, getWidth(), kBadgeH);
        g.setColour (pal::ChromeDark);
        g.fillRect (badgeBar);

        // Decorative Art-Deco chevron accent on badge left
        {
            const float cx = 8.0f;
            const float cy = kBadgeH * 0.5f;
            g.setColour (pal::NeonGold.withAlpha (0.55f));
            for (int i = 0; i < 3; ++i)
            {
                const float x = cx + static_cast<float> (i) * 4.0f;
                juce::Path chevron;
                chevron.addTriangle (x, cy - 5.0f, x + 3.0f, cy, x, cy + 5.0f);
                g.fillPath (chevron);
            }
        }

        // Badge text
        const int n       = static_cast<int> (tiles_.size());
        const int pending = static_cast<int> (pending_.size());
        juce::String badgeText;

        if (n == 0 && pending == 0)
            badgeText = "RESULTS VAULT";
        else if (pending > 0)
            badgeText = juce::String (n) + " READY  +"
                       + juce::String (pending) + " INCOMING";
        else
            badgeText = juce::String (n) + " SAMPLE"
                       + (n != 1 ? "S" : "") + " READY";

        g.setColour (pal::NeonGold);
        g.setFont (juce::Font (juce::FontOptions{}.withHeight (11.5f)));
        g.drawText (badgeText, badgeBar.withTrimmedLeft (28).reduced (0, 4),
                    juce::Justification::centredLeft, false);

        // Art-Deco vertical grid lines between columns
        const int tw = tileW();
        g.setColour (pal::ChromeMid.withAlpha (0.22f));
        for (int col = 1; col < kCols; ++col)
        {
            const int x = kGap + col * (tw + kGap) - kGap / 2;
            g.drawVerticalLine (x,
                                static_cast<float> (kBadgeH),
                                static_cast<float> (getHeight()));
        }

        // Horizontal row separators
        g.setColour (pal::ChromeMid.withAlpha (0.10f));
        const int rows = tiles_.empty()
                       ? 0
                       : (static_cast<int> (tiles_.size()) + kCols - 1) / kCols;
        const int yOrigin = kBadgeH + kGap;
        for (int row = 1; row <= rows; ++row)
        {
            const int y = yOrigin + row * (ResultTile::kTileH + kGap) - kGap / 2;
            g.drawHorizontalLine (y, 0.0f, static_cast<float> (getWidth()));
        }

        // Chrome rim at top of badge
        g.setColour (pal::ChromeLight.withAlpha (0.30f));
        g.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));
        g.setColour (pal::ChromeMid.withAlpha (0.50f));
        g.drawHorizontalLine (kBadgeH - 1, 0.0f, static_cast<float> (getWidth()));

        // ── Completion ceremony bar ──────────────────────────────────────────
        if (allDone_)
        {
            const float alpha    = ceremonyPhase_;                 // fade in with slide
            const int   contentH = computeHeight() - kExportBarH; // tile grid bottom
            // Slide in from below: starts off-screen, ends flush at contentH
            const float slideOff = static_cast<float> (kExportBarH) * (1.0f - ceremonyPhase_);
            const float barY     = static_cast<float> (contentH) + slideOff;
            const float barW     = static_cast<float> (getWidth());
            const float barH     = static_cast<float> (kExportBarH);

            const juce::Rectangle<float> bar { 0.0f, barY, barW, barH };

            // Body gradient — dark chrome with neon-gold tint
            {
                juce::ColourGradient grad {
                    pal::ChromeDark.interpolatedWith (pal::NeonGold, 0.08f), 0.0f, barY,
                    pal::ChromeVoid,                                          0.0f, barY + barH, false };
                grad.multiplyOpacity (alpha);
                g.setGradientFill (grad);
                g.fillRect (bar);
            }

            // Top separator line — neon gold
            g.setColour (pal::NeonGold.withAlpha (0.70f * alpha));
            g.drawHorizontalLine (static_cast<int> (barY),
                                  0.0f, barW);
            g.setColour (pal::NeonGold.withAlpha (0.22f * alpha));
            g.drawHorizontalLine (static_cast<int> (barY) + 1,
                                  0.0f, barW);

            // Art-Deco corner accent lines (left side)
            {
                const float lx = 10.0f;
                const float ty = barY + 8.0f;
                const float by = barY + barH - 8.0f;
                g.setColour (pal::NeonGold.withAlpha (0.60f * alpha));
                g.drawLine (lx, ty, lx + 14.0f, ty, 1.4f);
                g.drawLine (lx, ty, lx,         ty + 12.0f, 1.4f);
                g.drawLine (lx, by - 12.0f, lx, by, 1.4f);
                g.drawLine (lx, by, lx + 14.0f, by, 1.4f);
            }

            const float cy = barY + barH * 0.5f;
            const float cx = barW * 0.5f;

            // Stamp text — "✦ N SAMPLES READY"  (uses outer `n` = tiles_.size())
            {
                const juce::String stamp =
                    juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\xa6"))
                    + "  " + juce::String (n)
                    + " SAMPLE" + (n != 1 ? "S" : "")
                    + " READY";

                g.setFont (juce::Font (juce::FontOptions { 11.5f }).boldened());
                g.setColour (pal::NeonGold.withAlpha (0.90f * alpha));
                g.drawText (stamp,
                            juce::Rectangle<float> (30.0f, barY + 4.0f,
                                                    barW - 60.0f, 18.0f).toNearestInt(),
                            juce::Justification::centred, false);
            }

            // Export button pill — "◉ EXPORT COLLECTION"
            {
                const float pillW = 190.0f;
                const float pillH = 22.0f;
                const juce::Rectangle<float> pill {
                    cx - pillW * 0.5f, cy + 4.0f, pillW, pillH };

                // Pill fill
                juce::ColourGradient pg {
                    pal::NeonGold.withAlpha (0.22f * alpha), pill.getX(), pill.getY(),
                    pal::NeonGold.withAlpha (0.08f * alpha), pill.getX(), pill.getBottom(), false };
                g.setGradientFill (pg);
                g.fillRoundedRectangle (pill, 5.0f);

                // Pill border
                g.setColour (pal::NeonGold.withAlpha (0.65f * alpha));
                g.drawRoundedRectangle (pill, 5.0f, 1.2f);

                // Button text
                g.setFont (juce::Font (juce::FontOptions { 10.5f }.withStyle ("Bold")));
                g.setColour (pal::NeonGold.withAlpha (alpha));
                g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x89"))
                            + "  EXPORT COLLECTION",
                            pill.toNearestInt(), juce::Justification::centred, false);
            }
        }
    }
} // namespace switchblade::ui
