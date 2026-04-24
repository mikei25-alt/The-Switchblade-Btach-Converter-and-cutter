#include "SampleCard.h"
#include "Core/Palette.h"
#include "Analysis/PitchDetector.h"
#include "Analysis/ZeroCrossing.h"

#include <algorithm>
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
        const auto ratio = static_cast<double> (sample)
                         / static_cast<double> (file_->samples.getNumSamples());
        return wf.getX() + static_cast<float> (ratio) * wf.getWidth();
    }

    juce::int64 SampleCard::sampleForX (float x) const noexcept
    {
        if (! file_ || file_->samples.getNumSamples() <= 0)
            return 0;
        const auto wf = waveformBounds().toFloat();
        const double ratio = juce::jlimit (0.0,
            1.0, (static_cast<double> (x) - wf.getX()) / wf.getWidth());
        return static_cast<juce::int64> (
            std::llround (ratio * static_cast<double> (file_->samples.getNumSamples())));
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

        // Selection glow
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
            g.drawFittedText ("ANALYZING\xe2\x80\xa6",
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

        // Classification badge (pill) on the right — shows note name for Melodic
        // Only show the note name badge if pitch clarity is high enough (> 0.5)
        // indicating a true fundamental frequency, not noisy transients.
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
        g.setColour (classificationColour().withAlpha (0.25f));
        g.fillRoundedRectangle (badge.toFloat(), 8.0f);
        g.setColour (classificationColour());
        g.drawRoundedRectangle (badge.toFloat(), 8.0f, 1.0f);
        g.setFont (juce::Font (juce::FontOptions { 11.0f }).boldened());
        g.drawFittedText (badgeText, badge,
                          juce::Justification::centred, 1);

        // Filename
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

        // Fake-bloom: three increasing-alpha passes at identical bounds. Cheap,
        // CPU-only. The GL bloom pass in NeonBloomShader composes on top.
        const double totalS = file_->durationSeconds();
        g.setColour (pal::NeonCyan.withAlpha (0.18f));
        thumbnail_->drawChannels (g, r.expanded (0, 1), 0.0, totalS, 1.0f);
        g.setColour (pal::NeonCyan.withAlpha (0.40f));
        thumbnail_->drawChannels (g, r, 0.0, totalS, 1.0f);
        g.setColour (pal::NeonCyan);
        thumbnail_->drawChannels (g, r, 0.0, totalS, 1.0f);
    }

    void SampleCard::paintMarkers (juce::Graphics& g, juce::Rectangle<int> r) const
    {
        if (r.isEmpty() || transients_.empty() || ! file_)
            return;

        const auto top    = static_cast<float> (r.getY());
        const auto bottom = static_cast<float> (r.getBottom());

        for (std::size_t i = 0; i < transients_.size(); ++i)
        {
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
        const int hit = hitTestMarker (p);
        if (hit >= 0)
        {
            draggingIdx_ = hit;
            rebuildMonoCache();    // only cost when first needed
            repaint();
        }
        else if (onSelected)
        {
            onSelected();
        }
    }

    void SampleCard::mouseDrag (const juce::MouseEvent& e)
    {
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
