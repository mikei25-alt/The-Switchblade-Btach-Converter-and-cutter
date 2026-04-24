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
                            int sliceIndex)
        : fmt_           (fmt)
        , cache_         (cache)
        , file_          (std::move (file))
        , startSample_   (startSample)
        , endSample_     (endSample)
        , classification_(classification)
        , sliceIndex_    (sliceIndex)
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

        // Info strip
        auto infoArea = juce::Rectangle<float> (
            4.0f, b.getHeight() - 24.0f,
            b.getWidth() - 8.0f, 20.0f);

        const auto smallFont = juce::Font (juce::FontOptions{}.withHeight (11.0f));
        const auto tinyFont  = juce::Font (juce::FontOptions{}.withHeight (10.0f));

        // Index
        g.setFont (smallFont);
        g.setColour (pal::TextPrimary);
        const juce::String idxLabel = juce::String (sliceIndex_).paddedLeft ('0', 3);
        g.drawText (idxLabel, infoArea.removeFromLeft (26.0f).toNearestInt(),
                    juce::Justification::centredLeft, false);

        // Classification badge
        g.setFont (tinyFont);
        const juce::String tag = classTag();
        juce::GlyphArrangement ga;
        ga.addLineOfText (tinyFont, tag, 0.0f, 0.0f);
        const float tagW = ga.getBoundingBox (0, -1, true).getWidth() + 8.0f;
        const auto  tagBox = infoArea.removeFromLeft (tagW + 2.0f);
        g.setColour (classColour().withAlpha (0.20f));
        g.fillRoundedRectangle (tagBox, 2.0f);
        g.setColour (classColour());
        g.drawText (tag, tagBox.toNearestInt(), juce::Justification::centred, false);

        // Duration (right-aligned)
        g.setFont (tinyFont);
        g.setColour (pal::TextSecondary);
        g.drawText (durationStr(), infoArea.toNearestInt(),
                    juce::Justification::centredRight, false);

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
    }

    void ResultTile::mouseDown (const juce::MouseEvent& e)
    {
        if (e.getNumberOfClicks() >= 2)
        {
            if (onSelected) onSelected (file_, startSample_, endSample_);
        }
        else
        {
            if (onPlay) onPlay (file_, startSample_, endSample_);
        }
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
