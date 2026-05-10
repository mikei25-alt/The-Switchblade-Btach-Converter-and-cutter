#include "UI/ResultTile.h"
#include "Core/Palette.h"

#include <cmath>

namespace switchblade::ui
{
    namespace pal = switchblade::palette;

    ResultTile::ResultTile (juce::AudioFormatManager& fmt,
                            juce::AudioThumbnailCache& cache,
                            AudioFilePtr file,
                            juce::int64 startSample,
                            juce::int64 endSample,
                            switchblade::analysis::SourceClass classification,
                            int sliceIndex,
                            juce::String noteName)
        : fmt_           (fmt)
        , cache_         (cache)
        , file_          (std::move (file))
        , startSample_   (startSample)
        , endSample_     (endSample)
        , classification_(classification)
        , sliceIndex_    (sliceIndex)
        , noteName_      (std::move (noteName))
        , vblank_        (this, [this] { onVBlank(); })
    {
        thumbnail_ = std::make_unique<juce::AudioThumbnail> (64, fmt_, cache_);
        buildThumbnail();
    }

    ResultTile::~ResultTile() = default;

    //==========================================================================
    //  Public
    //==========================================================================
    void ResultTile::triggerEntryGlow() noexcept
    {
        entryGlow_ = 1.0f;
        repaint();
    }

    void ResultTile::setMultiSelected (bool s)
    {
        if (multiSelected_ == s) return;
        multiSelected_ = s;
        repaint();
        if (onMultiSelectChanged)
            onMultiSelectChanged();
    }

    //==========================================================================
    //  Private helpers
    //==========================================================================
    void ResultTile::buildThumbnail()
    {
        if (! file_ || ! file_->isValid()) return;

        const int numCh    = file_->samples.getNumChannels();
        const int srcStart = static_cast<int> (startSample_);
        const int sliceLen = static_cast<int> (
            std::min (endSample_,
                      static_cast<juce::int64> (file_->samples.getNumSamples()))
            - startSample_);

        if (numCh <= 0 || sliceLen <= 0) return;

        juce::AudioBuffer<float> buf (numCh, sliceLen);
        for (int ch = 0; ch < numCh; ++ch)
            buf.copyFrom (ch, 0, file_->samples, ch, srcStart, sliceLen);

        thumbnail_->reset (numCh, file_->sampleRate,
                           static_cast<juce::int64> (sliceLen));
        thumbnail_->addBlock (0, buf, 0, sliceLen);
    }

    void ResultTile::onVBlank()
    {
        if (entryGlow_ <= 0.0f) return;
        constexpr float kDecay = 0.014f;  // ~1.2s at 60 fps
        entryGlow_ = std::max (0.0f, entryGlow_ - kDecay);
        repaint();
    }

    //==========================================================================
    //  Component
    //==========================================================================
    void ResultTile::paint (juce::Graphics& g)
    {
        const auto b  = getLocalBounds().toFloat();
        const auto br = b.reduced (1.5f);

        // Chrome bevel border
        g.setGradientFill (pal::chromeBevel (b, false));
        g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.5f);

        // Frosted body
        g.setColour (pal::ChromeDark.withAlpha (0.88f));
        g.fillRoundedRectangle (br, 2.5f);

        // Waveform strip (top portion, leaving 26px for the info strip)
        const auto waveArea = juce::Rectangle<float> (
            4.0f, 4.0f,
            b.getWidth() - 8.0f,
            b.getHeight() - 30.0f);

        if (thumbnail_ && thumbnail_->isFullyLoaded())
        {
            // 3-pass NeonCyan bloom
            for (int pass = 0; pass < 3; ++pass)
            {
                const float alpha = 0.70f - pass * 0.18f;
                const float pad   = static_cast<float> (pass) * 0.8f;
                g.setColour (pal::NeonCyan.withAlpha (alpha));
                thumbnail_->drawChannels (
                    g,
                    waveArea.expanded (0.0f, pad).toNearestInt(),
                    0.0, thumbnail_->getTotalLength(), 1.0f);
            }
        }
        else
        {
            g.setColour (pal::ChromeMid.withAlpha (0.35f));
            g.fillRect (waveArea);
        }

        // Normalization badge — gold "N" pill, top-left of waveform area
        if (normalized_)
        {
            const auto normR = juce::Rectangle<float> (5.0f, 5.0f, 18.0f, 13.0f);
            g.setColour (pal::NeonGold.withAlpha (0.25f));
            g.fillRoundedRectangle (normR, 3.0f);
            g.setColour (pal::NeonGold);
            g.drawRoundedRectangle (normR, 3.0f, 0.8f);
            g.setFont (juce::Font (juce::FontOptions{}.withHeight (9.0f).withStyle ("Bold")));
            g.drawText ("N", normR.toNearestInt(), juce::Justification::centred, false);
        }

        // Info strip — layout: [Filename] | [Note] | [Length]
        auto infoArea = juce::Rectangle<float> (
            4.0f, b.getHeight() - 24.0f,
            b.getWidth() - 8.0f, 20.0f);

        const auto tinyFont = juce::Font (juce::FontOptions{}.withHeight (10.0f));
        g.setFont (tinyFont);

        // Duration — right-aligned, fixed slot
        const juce::String dur = durationStr();
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (tinyFont, dur, 0.0f, 0.0f);
            const float durW = ga.getBoundingBox (0, -1, true).getWidth() + 2.0f;
            const auto durBox = infoArea.removeFromRight (durW);
            g.setColour (pal::TextSecondary);
            g.drawText (dur, durBox.toNearestInt(), juce::Justification::centredRight, false);
        }

        // Note badge (melodic only) — right of filename, left of duration
        if (noteName_.isNotEmpty())
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (tinyFont, noteName_, 0.0f, 0.0f);
            const float nw = ga.getBoundingBox (0, -1, true).getWidth() + 6.0f;
            const auto noteBox = infoArea.removeFromRight (nw + 2.0f);
            g.setColour (classColour().withAlpha (0.20f));
            g.fillRoundedRectangle (noteBox.reduced (1.0f, 2.0f), 2.0f);
            g.setColour (classColour());
            g.drawText (noteName_, noteBox.toNearestInt(), juce::Justification::centred, false);
        }

        // Filename stem — left-aligned, remaining space
        const juce::String stem = file_
            ? juce::File (juce::String (file_->path.string())).getFileNameWithoutExtension()
            : juce::String (sliceIndex_).paddedLeft ('0', 3);
        g.setColour (pal::TextPrimary);
        g.drawText (stem, infoArea.toNearestInt(), juce::Justification::centredLeft, true);

        // Entry glow overlay — white-hot → NeonCyan
        if (entryGlow_ > 0.0f)
        {
            const auto glowColor = juce::Colours::white
                .interpolatedWith (pal::NeonCyan, 1.0f - entryGlow_)
                .withAlpha (entryGlow_ * 0.55f);
            g.setColour (glowColor);
            g.fillRoundedRectangle (br, 2.5f);

            // Neon rim
            g.setColour (pal::NeonCyan.withAlpha (entryGlow_ * 0.8f));
            g.drawRoundedRectangle (br, 2.5f, 1.0f);
        }

        // Ctrl+click multi-select — 2px NeonGold border on top of everything
        if (multiSelected_)
        {
            g.setColour (pal::Selection.withAlpha (0.30f));
            g.drawRoundedRectangle (br.expanded (1.0f), 3.0f, 3.0f);
            g.setColour (pal::Selection);
            g.drawRoundedRectangle (br, 2.5f, 2.0f);
        }
    }

    void ResultTile::mouseDown (const juce::MouseEvent& e)
    {
        dragStarted_ = false;

        // Ctrl+click toggles multi-select for "Export Selection" — checked first
        // so Ctrl+click never fires playback or selection callbacks.
        if (e.mods.isCtrlDown())
        {
            setMultiSelected (! multiSelected_);
            return;
        }

        if (e.getNumberOfClicks() >= 2)
        {
            if (onSelected) onSelected (file_, startSample_, endSample_);
        }
        else
        {
            if (onPlay) onPlay (file_, startSample_, endSample_);
        }
    }

    void ResultTile::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragStarted_ || ! onExternalDrag) return;
        if (e.getDistanceFromDragStart() < 6) return;
        dragStarted_ = true;
        onExternalDrag (*this);
    }

    //==========================================================================
    //  Accessors
    //==========================================================================
    juce::String ResultTile::durationStr() const noexcept
    {
        if (! file_ || file_->sampleRate <= 0.0) return {};
        const double durMs = static_cast<double> (endSample_ - startSample_)
                           / file_->sampleRate * 1000.0;
        if (durMs < 1000.0)
            return juce::String (static_cast<int> (std::round (durMs))) + "ms";
        return juce::String (durMs / 1000.0, 1) + "s";
    }

    const char* ResultTile::classTag() const noexcept
    {
        using C = switchblade::analysis::SourceClass;
        switch (classification_)
        {
            case C::Percussive: return "PERC";
            case C::Melodic:    return "MEL";
            case C::Texture:    return "TEX";
            default:            return "---";
        }
    }

    juce::Colour ResultTile::classColour() const noexcept
    {
        using C = switchblade::analysis::SourceClass;
        switch (classification_)
        {
            case C::Percussive: return pal::NeonCyan;
            case C::Melodic:    return pal::NeonGold;
            case C::Texture:    return pal::NeonMint;
            default:            return pal::TextSecondary;
        }
    }
} // namespace switchblade::ui
