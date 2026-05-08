#include "SampleCard.h"
#include "Core/Palette.h"
#include "Analysis/PitchDetector.h"
#include "Analysis/ZeroCrossing.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace switchblade::ui
{
    namespace pal = switchblade::palette;

    namespace
    {
        constexpr int   kHeaderH           = 24;
        constexpr int   kFooterH           = 30;
        constexpr int   kHorizontalPad     = 12;
        constexpr int   kMarkerHitPx       = 6;
        constexpr float kLiftSpeed         = 0.12f;   // per-frame towards target
        constexpr float kLiftMaxPixels     = 3.0f;
        constexpr float kShadowBase        = 6.0f;
        constexpr float kShadowLiftFactor  = 10.0f;
    } // namespace

    //==========================================================================
    //  Construction / destruction
    //==========================================================================
    SampleCard::SampleCard (juce::AudioFormatManager& fm,
                            juce::AudioThumbnailCache& cache)
        : formatManager_ (fm),
          thumbnailCache_ (cache),
          vblank_ (this, [this] { animateLift(); })
    {
        addAndMakeVisible (extractBtn_);
        extractBtn_.onClick = [this]
        {
            if (onExtractClicked)
                onExtractClicked();
        };
        setRepaintsOnMouseActivity (false);  // we drive repaints manually
    }

    SampleCard::~SampleCard() = default;

    //==========================================================================
    //  Data setters
    //==========================================================================
    void SampleCard::setFile (AudioFilePtr file)
    {
        file_ = std::move (file);
        monoCache_.clear();
        viewStart_ = 0.0;
        viewEnd_   = 1.0;
        isPanning_ = false;
        rebuildThumbnail();
        repaint();
    }

    void SampleCard::setTransients (std::vector<switchblade::analysis::Transient> t)
    {
        transients_ = std::move (t);
        repaint();
    }

    void SampleCard::setClassification (switchblade::analysis::SourceClass c)
    {
        classification_ = c;
        repaint (headerBounds());
    }

    void SampleCard::setPitchHz (std::optional<float> hz) noexcept
    {
        pitchHz_ = hz;
        repaint (headerBounds());
    }

    void SampleCard::setPitchClarity (std::optional<float> clarity) noexcept
    {
        pitchClarity_ = clarity;
        repaint (headerBounds());
    }

    void SampleCard::setSelected (bool s)
    {
        if (selected_ == s)
            return;
        selected_ = s;
        repaint();
    }

    void SampleCard::setMultiSelected (bool s)
    {
        if (multiSelected_ == s)
            return;
        multiSelected_ = s;
        repaint();
        if (onMultiSelectChanged)
            onMultiSelectChanged();
    }

    void SampleCard::setLoading (bool isLoading) noexcept
    {
        loading_ = isLoading;
        repaint();
    }

    void SampleCard::setDisplayPath (const std::filesystem::path& p)
    {
        displayPath_ = p;
        repaint (headerBounds());
    }

    void SampleCard::triggerEntryGlow() noexcept
    {
        entryGlow_ = 1.0f;
        liftPhase_ = 1.0f;   // start lifted; will settle under animation
        // VBlankAttachment already running — will decay every frame
    }

    //==========================================================================
    //  Thumbnail / mono cache
    //==========================================================================
    void SampleCard::rebuildThumbnail()
    {
        thumbnail_.reset();
        if (! file_ || ! file_->isValid())
            return;

        thumbnail_ = std::make_unique<juce::AudioThumbnail> (
            512, formatManager_, thumbnailCache_);
        thumbnail_->reset (file_->samples.getNumChannels(),
                           file_->sampleRate,
                           file_->samples.getNumSamples());
        thumbnail_->addBlock (0, file_->samples, 0,
                              file_->samples.getNumSamples());
    }

    void SampleCard::rebuildMonoCache()
    {
        if (! file_ || ! file_->isValid() || ! monoCache_.empty())
            return;
        switchblade::analysis::mixToMono (file_->samples, monoCache_);
    }

    //==========================================================================
    //  Layout helpers
    //==========================================================================
    juce::Rectangle<int> SampleCard::headerBounds() const noexcept
    {
        return getLocalBounds().withHeight (kHeaderH).reduced (6, 4);
    }

    juce::Rectangle<int> SampleCard::playBtnBounds() const noexcept
    {
        return headerBounds().removeFromLeft (22);
    }

    juce::Rectangle<int> SampleCard::waveformBounds() const noexcept
    {
        return getLocalBounds()
                 .withTrimmedTop (kHeaderH)
                 .withTrimmedBottom (kFooterH)
                 .reduced (kHorizontalPad, 4);
    }

    float SampleCard::xForSample (juce::int64 sample) const noexcept
    {
        if (! file_ || file_->samples.getNumSamples() <= 0)
            return 0.0f;
        const auto wf = waveformBounds().toFloat();
        const double frac   = static_cast<double> (sample)
                            / static_cast<double> (file_->samples.getNumSamples());
        const double range  = viewEnd_ - viewStart_;
        const double vFrac  = (frac - viewStart_) / range;
        return wf.getX() + static_cast<float> (vFrac) * wf.getWidth();
    }

    juce::int64 SampleCard::sampleForX (float x) const noexcept
    {
        if (! file_ || file_->samples.getNumSamples() <= 0)
            return 0;
        const auto wf = waveformBounds().toFloat();
        const double vFrac = (static_cast<double> (x) - wf.getX()) / wf.getWidth();
        const double frac  = viewStart_ + vFrac * (viewEnd_ - viewStart_);
        return static_cast<juce::int64> (
            std::llround (std::clamp (frac, 0.0, 1.0)
                          * static_cast<double> (file_->samples.getNumSamples())));
    }

    int SampleCard::hitTestMarker (juce::Point<float> p) const noexcept
    {
        const auto wf = waveformBounds().toFloat();
        if (! wf.expanded (kMarkerHitPx, 0).contains (p))
            return -1;

        int   bestIdx = -1;
        float bestDx  = static_cast<float> (kMarkerHitPx) + 0.5f;
        for (std::size_t i = 0; i < transients_.size(); ++i)
        {
            const float x  = xForSample (transients_[i].sampleIndex);
            const float dx = std::abs (x - p.x);
            if (dx < bestDx)
            {
                bestDx  = dx;
                bestIdx = static_cast<int> (i);
            }
        }
        return bestIdx;
    }

    //==========================================================================
    //  Lift animation
    //==========================================================================
    void SampleCard::animateLift()
    {
        bool needsRepaint = false;

        // Lift phase
        const float liftTarget = (hovered_ || entryGlow_ > 0.5f) ? 1.0f : 0.0f;
        const float liftDelta  = liftTarget - liftPhase_;
        if (std::abs (liftDelta) > 1.0e-3f)
        {
            liftPhase_ += liftDelta * kLiftSpeed;
            needsRepaint = true;
        }

        // Cooling-glow decay (~1.2 s from white-hot to zero at 60 fps)
        if (entryGlow_ > 0.0f)
        {
            entryGlow_ = std::max (0.0f, entryGlow_ - 0.014f);
            needsRepaint = true;
        }

        if (needsRepaint)
            repaint();
    }

    //==========================================================================
    //  Component
    //==========================================================================
    void SampleCard::resized()
    {
        // Footer: extract button pinned right
        const auto footer = getLocalBounds()
                              .removeFromBottom (kFooterH)
                              .reduced (kHorizontalPad, 4);
        extractBtn_.setBounds (footer.withSizeKeepingCentre (
            juce::jmin (footer.getWidth(), 110), footer.getHeight()).withX (
            footer.getRight() - juce::jmin (footer.getWidth(), 110)));
    }

    void SampleCard::paint (juce::Graphics& g)
    {
        const auto full  = getLocalBounds().toFloat();
        const float lift = liftPhase_ * kLiftMaxPixels;
        const auto body  = full.reduced (3.0f).translated (0.0f, -lift);

        // ----- Shadow (expands with lift) --------------------------------
        {
            juce::Path p;
            p.addRoundedRectangle (body, 10.0f);
            const float radius = kShadowBase + liftPhase_ * kShadowLiftFactor;
            juce::DropShadow {
                pal::GlassShade, static_cast<int> (radius),
                { 0, static_cast<int> (lift + 2.0f) }
            }.drawForPath (g, p);
        }

        // ----- Chrome frame ----------------------------------------------
        {
            juce::Path frame;
            frame.addRoundedRectangle (body, 10.0f);
            g.setGradientFill (pal::chromeBevel (body, false));
            g.fillPath (frame);
        }

        // ----- Frosted body (inset) --------------------------------------
        const auto inner = body.reduced (2.0f);
        {
            juce::Path fp;
            fp.addRoundedRectangle (inner, 8.0f);
            juce::ColourGradient fill {
                pal::GlassTint,     inner.getX(), inner.getY(),
                pal::GlassTintDeep, inner.getX(), inner.getBottom(), false };
            g.setGradientFill (fill);
            g.fillPath (fp);
        }

        // Inner rim highlight
        g.setColour (pal::GlassRim);
        g.drawRoundedRectangle (inner, 8.0f, 1.0f);

        // Preview-focus glow (single selection — cyan)
        if (selected_)
        {
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (pal::NeonCyan.withAlpha (0.12f * static_cast<float> (i)));
                g.drawRoundedRectangle (
                    inner.expanded (static_cast<float> (i) * 1.2f),
                    8.0f + static_cast<float> (i) * 1.2f,
                    1.2f * static_cast<float> (i));
            }
            g.setColour (pal::NeonCyan);
            g.drawRoundedRectangle (inner, 8.0f, 1.4f);
        }

        // Ctrl+click multi-select border — 2px NeonGold solid outline
        if (multiSelected_)
        {
            g.setColour (pal::Selection.withAlpha (0.25f));
            g.drawRoundedRectangle (inner.expanded (2.0f), 9.5f, 4.0f);
            g.setColour (pal::Selection);
            g.drawRoundedRectangle (inner, 8.0f, 2.0f);
        }

        // ----- Cooling-glow entry ring -----------------------------------
        if (entryGlow_ > 0.0f)
        {
            // Blend white-hot → NeonCyan as it cools
            const auto hotColour = pal::ChromeSpec.interpolatedWith (
                pal::NeonCyan, 1.0f - entryGlow_);
            for (int i = 4; i >= 1; --i)
            {
                const float r = static_cast<float> (i) * 2.5f;
                g.setColour (hotColour.withAlpha (entryGlow_ * 0.18f
                             * static_cast<float> (i)));
                g.drawRoundedRectangle (inner.expanded (r), 8.0f + r, r);
            }
            g.setColour (hotColour.withAlpha (entryGlow_ * 0.9f));
            g.drawRoundedRectangle (inner, 8.0f, 1.8f);
        }

        // ----- Loading overlay (ANALYZING…) ------------------------------
        if (loading_)
        {
            g.setColour (pal::ChromeVoid.withAlpha (0.72f));
            juce::Path fp;
            fp.addRoundedRectangle (inner, 8.0f);
            g.fillPath (fp);

            // Pulsing neon text using host time
            const float pulse = 0.55f + 0.45f * std::sin (
                static_cast<float> (juce::Time::getMillisecondCounterHiRes() * 0.004));
            g.setColour (pal::NeonCyan.withAlpha (pulse));
            g.setFont (juce::Font (juce::FontOptions { 15.0f }).boldened());
            g.drawFittedText (juce::String (juce::CharPointer_UTF8 ("ANALYZING\xe2\x80\xa6")),
                              inner.toNearestInt(),
                              juce::Justification::centred, 1);
        }

        // ----- Content (header + waveform) shifted with the lift ------------
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.addTransform (juce::AffineTransform::translation (0.0f, -lift));
            paintHeader  (g, headerBounds());
            paintWaveform (g, waveformBounds());
            paintMarkers  (g, waveformBounds());
        }
    }

    void SampleCard::paintHeader (juce::Graphics& g, juce::Rectangle<int> r) const
    {
        const auto sourcePath = file_ ? file_->path : displayPath_;
        if (sourcePath.empty())
            return;

        const auto name = juce::String (sourcePath.filename().string());

        // ── Green play triangle (left edge) ───────────────────────────────────
        // Fixed 22px slot so the triangle is clearly visible regardless of card height.
        const auto playR = r.removeFromLeft (22);
        r.removeFromLeft (3);   // gap before filename

        if (file_)
        {
            const auto  pf  = playR.toFloat();
            const float cx  = pf.getCentreX();
            const float cy  = pf.getCentreY();
            const float h   = pf.getHeight() * 0.70f;
            const float w   = h * 0.866f;

            // Background circle glow
            g.setColour (pal::NeonMint.withAlpha (hovered_ ? 0.30f : 0.16f));
            g.fillEllipse (pf.reduced (1.0f));

            // Filled right-pointing triangle
            juce::Path tri;
            tri.addTriangle (cx - w * 0.38f, cy - h * 0.50f,
                             cx - w * 0.38f, cy + h * 0.50f,
                             cx + w * 0.62f, cy);
            g.setColour (hovered_ ? pal::ChromeSpec : pal::NeonMint);
            g.fillPath (tri);

            // Thin circle border
            g.setColour (pal::NeonMint.withAlpha (0.55f));
            g.drawEllipse (pf.reduced (1.0f), 1.0f);
        }
        else
        {
            g.setColour (pal::TextDisabled);
            g.drawEllipse (playR.toFloat().reduced (2.0f), 1.0f);
        }

        // ── Classification badge (right edge) ────────────────────────────────
        using C = switchblade::analysis::SourceClass;
        juce::String badgeText = classificationTag();
        if (classification_ == C::Melodic && pitchHz_.has_value()
            && pitchClarity_.value_or (0.0f) > 0.5f)
        {
            badgeText = "MEL "
                + juce::String (switchblade::analysis::PitchDetector::noteNameFromHz (
                                *pitchHz_));
        }

        const int badgeW = 96;
        const auto badge = r.removeFromRight (badgeW).reduced (0, 2);
        badgeBounds_ = badge;   // cache for mouseDown hit-test

        // Subtle hover highlight so user sees it's clickable
        const bool badgeHovered = hovered_ && badge.contains (getMouseXYRelative());
        g.setColour (classificationColour().withAlpha (badgeHovered ? 0.45f : 0.25f));
        g.fillRoundedRectangle (badge.toFloat(), 8.0f);
        g.setColour (classificationColour());
        g.drawRoundedRectangle (badge.toFloat(), 8.0f, badgeHovered ? 1.8f : 1.0f);
        g.setFont (juce::Font (juce::FontOptions { 11.0f }).boldened());
        g.drawFittedText (badgeText, badge,
                          juce::Justification::centred, 1);

        // ── Filename ─────────────────────────────────────────────────────────
        g.setColour (pal::TextPrimary);
        g.setFont (juce::Font (juce::FontOptions { 13.0f }).boldened());
        g.drawFittedText (name, r.reduced (2, 0),
                          juce::Justification::centredLeft, 1);
    }

    void SampleCard::paintWaveform (juce::Graphics& g, juce::Rectangle<int> r)
    {
        if (r.isEmpty() || thumbnail_ == nullptr || ! file_)
            return;

        // Well background
        g.setColour (pal::ChromeVoid.withAlpha (0.55f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (pal::ChromeMid.withAlpha (0.5f));
        g.drawRoundedRectangle (r.toFloat(), 4.0f, 1.0f);

        const double totalS  = file_->durationSeconds();
        const double startS  = viewStart_ * totalS;
        const double endS    = viewEnd_   * totalS;

        // Fake-bloom: three increasing-alpha passes. Draws only the visible range.
        g.setColour (pal::NeonCyan.withAlpha (0.18f));
        thumbnail_->drawChannels (g, r.expanded (0, 1), startS, endS, 1.0f);
        g.setColour (pal::NeonCyan.withAlpha (0.40f));
        thumbnail_->drawChannels (g, r, startS, endS, 1.0f);
        g.setColour (pal::NeonCyan);
        thumbnail_->drawChannels (g, r, startS, endS, 1.0f);

        // ── Normalization pill badge (bottom-left of waveform) ────────────────
        if (normDb_ < 0.0f)
        {
            const juce::String normStr =
                juce::String (juce::CharPointer_UTF8 ("\xe2\x8a\x95"))   // ⊕
                + " " + juce::String (static_cast<int> (normDb_)) + "dB";

            g.setFont (juce::Font (juce::FontOptions { 10.0f }).boldened());
            juce::GlyphArrangement ga;
            ga.addLineOfText (g.getCurrentFont(), normStr, 0.0f, 0.0f);
            const float tw = ga.getBoundingBox (0, -1, true).getWidth();
            const auto pillR = juce::Rectangle<float> (
                static_cast<float> (r.getX()) + 3.0f,
                static_cast<float> (r.getBottom()) - 17.0f,
                tw + 8.0f, 13.0f);
            g.setColour (pal::NeonGold.withAlpha (0.22f));
            g.fillRoundedRectangle (pillR, 3.0f);
            g.setColour (pal::NeonGold);
            g.drawRoundedRectangle (pillR, 3.0f, 0.8f);
            g.drawText (normStr, pillR.toNearestInt(), juce::Justification::centred, false);
        }

        // ── Zoom overlay ──────────────────────────────────────────────────────
        const double range = viewEnd_ - viewStart_;
        if (range < 0.999)
        {
            const float zoom = static_cast<float> (1.0 / range);

            // Zoom badge — top-right corner
            const juce::String zoomStr =
                (zoom < 10.0f ? juce::String (zoom, 1) : juce::String (static_cast<int> (zoom)))
                + juce::String (juce::CharPointer_UTF8 ("\xc3\x97"));  // UTF-8 "×"

            const auto badgeR = r.removeFromRight (38).removeFromTop (16).reduced (2, 1);
            g.setColour (pal::ChromeVoid.withAlpha (0.72f));
            g.fillRoundedRectangle (badgeR.toFloat(), 3.0f);
            g.setColour (pal::NeonGold);
            g.setFont (juce::Font (juce::FontOptions { 10.0f }).boldened());
            g.drawFittedText (zoomStr, badgeR, juce::Justification::centred, 1);

            // Scroll-position bar — bottom of waveform
            const auto scrollR = r.removeFromBottom (3).toFloat();
            g.setColour (pal::ChromeMid.withAlpha (0.40f));
            g.fillRect (scrollR);
            g.setColour (pal::NeonGold.withAlpha (0.70f));
            const float hx = scrollR.getX() + static_cast<float> (viewStart_) * scrollR.getWidth();
            const float hw = static_cast<float> (range) * scrollR.getWidth();
            g.fillRect (hx, scrollR.getY(), std::max (2.0f, hw), scrollR.getHeight());
        }
    }

    void SampleCard::paintMarkers (juce::Graphics& g, juce::Rectangle<int> r) const
    {
        if (r.isEmpty() || transients_.empty() || ! file_)
            return;

        const auto top    = static_cast<float> (r.getY());
        const auto bottom = static_cast<float> (r.getBottom());
        const double totalS = static_cast<double> (file_->samples.getNumSamples());

        for (std::size_t i = 0; i < transients_.size(); ++i)
        {
            // Skip markers that are outside the visible range (with 1px margin)
            const double frac = static_cast<double> (transients_[i].sampleIndex) / totalS;
            if (frac < viewStart_ - 0.001 || frac > viewEnd_ + 0.001)
                continue;

            const float x = xForSample (transients_[i].sampleIndex);
            const bool  active = (static_cast<int> (i) == draggingIdx_);

            // Glow
            g.setColour (pal::NeonGold.withAlpha (active ? 0.55f : 0.32f));
            g.drawLine (x, top, x, bottom, active ? 4.0f : 3.0f);
            g.setColour (pal::NeonGold);
            g.drawLine (x, top, x, bottom, active ? 1.6f : 1.0f);

            // Head cap
            const auto cap = juce::Rectangle<float> { x - 3.0f, top - 3.0f, 6.0f, 6.0f };
            g.setColour (pal::NeonGold);
            g.fillEllipse (cap);
        }
    }

    //==========================================================================
    //  Mouse handling
    //==========================================================================
    void SampleCard::mouseDown (const juce::MouseEvent& e)
    {
        const auto p = e.position;

        // Play button takes priority — fires before marker / selection logic
        if (file_ && playBtnBounds().toFloat().contains (p))
        {
            if (onPlayClicked)
                onPlayClicked();
            return;
        }

        // Ctrl+click always toggles multi-select — checked BEFORE hitTestMarker
        // so clicking near a marker with Ctrl held selects rather than dragging.
        if (e.mods.isCtrlDown())
        {
            setMultiSelected (! multiSelected_);
            return;
        }

        // Classification badge → algorithm-override dropdown
        if (badgeBounds_.contains (e.getPosition()))
        {
            using M = switchblade::analysis::AnalysisMode;
            juce::PopupMenu menu;
            menu.addItem (1, "Auto");
            menu.addItem (2, "Percussive");
            menu.addItem (3, "Melodic");
            menu.addItem (4, "Texture");

            const auto screenArea = localAreaToGlobal (badgeBounds_);
            menu.showMenuAsync (
                juce::PopupMenu::Options{}
                    .withTargetComponent (this)
                    .withTargetScreenArea (screenArea),
                [this] (int result)
                {
                    if (result <= 0 || ! onModeChangeRequested) return;
                    constexpr std::array<M, 4> kModes { M::Auto, M::Percussive,
                                                        M::Melodic, M::Texture };
                    onModeChangeRequested (kModes[static_cast<std::size_t> (result - 1)]);
                });
            return;
        }

        const int hit = hitTestMarker (p);

        if (hit >= 0)
        {
            draggingIdx_ = hit;
            rebuildMonoCache();
            repaint();
        }
        else
        {
            // If zoomed, drag on the waveform background pans the view
            const bool zoomed = (viewEnd_ - viewStart_) < 0.999;
            if (zoomed && waveformBounds().toFloat().contains (p))
            {
                isPanning_ = true;
                panLastX_  = p.x;
                // Don't fire onSelected so the card stays highlighted
            }
            else
            {
                if (onSelected)
                    onSelected();
            }
        }
    }

    void SampleCard::mouseDrag (const juce::MouseEvent& e)
    {
        // ── Pan (waveform background drag when zoomed) ─────────────────────────
        if (isPanning_ && draggingIdx_ < 0)
        {
            const auto wf = waveformBounds().toFloat();
            if (wf.getWidth() > 0.0f)
            {
                const double delta = static_cast<double> (panLastX_ - e.position.x)
                                   / wf.getWidth()
                                   * (viewEnd_ - viewStart_);
                panLastX_ = e.position.x;

                const double range = viewEnd_ - viewStart_;
                viewStart_ = std::clamp (viewStart_ + delta, 0.0, 1.0 - range);
                viewEnd_   = viewStart_ + range;
                repaint();
            }
            return;
        }

        if (draggingIdx_ < 0 || ! file_
            || draggingIdx_ >= static_cast<int> (transients_.size()))
            return;

        juce::int64 raw = sampleForX (e.position.x);
        const double sr = file_->sampleRate;
        const juce::int64 snapRadius = static_cast<juce::int64> (
            std::llround (0.005 * sr));   // 5 ms

        juce::int64 snapped = raw;
        if (! monoCache_.empty())
        {
            snapped = switchblade::analysis::snapToZeroCrossing (
                std::span<const float> (monoCache_.data(), monoCache_.size()),
                raw, snapRadius);
        }

        auto& t = transients_[static_cast<std::size_t> (draggingIdx_)];
        t.rawSampleIndex = raw;
        t.sampleIndex    = snapped;
        t.timeSeconds    = static_cast<double> (snapped) / sr;
        repaint();
    }

    void SampleCard::mouseUp (const juce::MouseEvent&)
    {
        isPanning_ = false;

        if (draggingIdx_ >= 0
            && draggingIdx_ < static_cast<int> (transients_.size()))
        {
            if (onMarkerMoved)
                onMarkerMoved (draggingIdx_,
                               transients_[static_cast<std::size_t> (draggingIdx_)].sampleIndex);
        }
        draggingIdx_ = -1;
        repaint();
    }

    void SampleCard::mouseEnter (const juce::MouseEvent&)
    {
        hovered_ = true;
    }

    void SampleCard::mouseExit (const juce::MouseEvent&)
    {
        hovered_ = false;
    }

    void SampleCard::mouseDoubleClick (const juce::MouseEvent& e)
    {
        // Double-click on waveform area resets zoom to full view
        if (waveformBounds().toFloat().contains (e.position))
        {
            viewStart_ = 0.0;
            viewEnd_   = 1.0;
            isPanning_ = false;
            repaint();
        }
    }

    void SampleCard::mouseWheelMove (const juce::MouseEvent& e,
                                     const juce::MouseWheelDetails& wheel)
    {
        if (! file_ || file_->samples.getNumSamples() <= 0)
            return;

        const auto wf = waveformBounds().toFloat();
        if (! wf.contains (e.position))
            return;

        const double oldRange = viewEnd_ - viewStart_;

        // Zoom in on scroll-up, out on scroll-down. Factor ≈ 25 % per tick.
        const double factor   = (wheel.deltaY > 0.0f) ? 0.75 : 1.33;
        const double minRange = std::max (1.0 / static_cast<double> (
                                    file_->samples.getNumSamples()), 0.001);
        const double newRange = std::clamp (oldRange * factor, minRange, 1.0);

        // Keep the position under the cursor fixed while zooming
        const double cursorFrac = viewStart_
            + (static_cast<double> (e.position.x - wf.getX()) / wf.getWidth())
              * oldRange;

        viewStart_ = std::clamp (cursorFrac - newRange * 0.5, 0.0, 1.0 - newRange);
        viewEnd_   = viewStart_ + newRange;

        repaint();
    }

    //==========================================================================
    //  Classification helpers
    //==========================================================================
    juce::String SampleCard::classificationTag() const noexcept
    {
        using C = switchblade::analysis::SourceClass;
        switch (classification_)
        {
            case C::Percussive: return "PERCUSSIVE";
            case C::Melodic:    return "MELODIC";
            case C::Texture:    return "TEXTURE";
            case C::Unknown:
            default:            return "UNKNOWN";
        }
    }

    juce::Colour SampleCard::classificationColour() const noexcept
    {
        using C = switchblade::analysis::SourceClass;
        switch (classification_)
        {
            case C::Percussive: return pal::NeonGold;
            case C::Melodic:    return pal::NeonMagenta;
            case C::Texture:    return pal::NeonMint;
            case C::Unknown:
            default:            return pal::TextSecondary;
        }
    }
} // namespace switchblade::ui
