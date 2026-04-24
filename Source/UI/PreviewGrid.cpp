#include "PreviewGrid.h"
#include "Core/Palette.h"

#include <algorithm>
#include <cmath>

namespace switchblade::ui
{
    namespace pal = switchblade::palette;

    //==========================================================================
    //  GridVoiceBank
    //==========================================================================
    void GridVoiceBank::trigger (AudioFilePtr file,
                                 juce::int64 start, juce::int64 end)
    {
        if (! file || start >= end)
            return;

        const juce::ScopedLock sl (lock_);

        // Find first inactive voice; steal the oldest active if all full
        Voice* target = nullptr;
        for (auto& v : voices_)
        {
            if (! v.active) { target = &v; break; }
        }
        if (target == nullptr)
            target = &voices_[0];   // steal

        target->file     = std::move (file);
        target->start    = start;
        target->end      = end;
        target->playhead = start;
        target->active   = true;
    }

    void GridVoiceBank::stopAll() noexcept
    {
        const juce::ScopedLock sl (lock_);
        for (auto& v : voices_)
            v.active = false;
    }

    void GridVoiceBank::prepareToPlay (int /*blockSize*/, double sampleRate)
    {
        deviceSampleRate_ = sampleRate;
    }

    void GridVoiceBank::releaseResources()
    {
        stopAll();
    }

    void GridVoiceBank::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
    {
        info.clearActiveBufferRegion();

        const juce::ScopedTryLock tl (lock_);
        if (! tl.isLocked())
            return;   // contended — skip block (rare, inaudible at 44.1 kHz)

        const int numOut = info.buffer->getNumChannels();
        const int needed = info.numSamples;

        for (auto& v : voices_)
        {
            if (! v.active || ! v.file)
                continue;

            const auto& src   = v.file->samples;
            const int numSrc  = src.getNumChannels();
            const int numFade = static_cast<int> (std::round (0.005 * deviceSampleRate_));

            int written = 0;
            while (written < needed && v.active)
            {
                const int remaining    = static_cast<int> (v.end - v.playhead);
                const int canWrite     = std::min (needed - written, remaining);

                if (canWrite <= 0) { v.active = false; break; }

                for (int ch = 0; ch < numOut; ++ch)
                {
                    const int srcCh = (numSrc > 0) ? ch % numSrc : 0;
                    const auto* r   = src.getReadPointer (srcCh,
                                          static_cast<int> (v.playhead));
                    auto* w         = info.buffer->getWritePointer (ch,
                                          info.startSample + written);

                    for (int i = 0; i < canWrite; ++i)
                    {
                        float gain = 1.0f;
                        // 5 ms fade-in from slice start
                        const juce::int64 fromStart = (v.playhead + i) - v.start;
                        if (fromStart < numFade)
                            gain = static_cast<float> (fromStart) / static_cast<float> (numFade);
                        // 5 ms fade-out before slice end
                        const juce::int64 fromEnd = v.end - (v.playhead + i);
                        if (fromEnd < numFade)
                            gain *= static_cast<float> (fromEnd) / static_cast<float> (numFade);

                        w[i] += r[i] * gain;
                    }
                }

                v.playhead += canWrite;
                written    += canWrite;
                if (v.playhead >= v.end)
                    v.active = false;
            }
        }
    }

    //==========================================================================
    //  PreviewGrid
    //==========================================================================
    PreviewGrid::PreviewGrid()
    {
        for (int i = 0; i < kNumPads; ++i)
            pads_[static_cast<std::size_t> (i)].label =
                juce::String::charToString (kKeyMap[static_cast<std::size_t> (i)]);

        setWantsKeyboardFocus (true);
        startTimer (30);   // 30 ms tick for flash decay (~33 fps, light)
    }

    PreviewGrid::~PreviewGrid() = default;

    //==========================================================================
    //  Source binding
    //==========================================================================
    void PreviewGrid::setSource (AudioFilePtr file,
                                 std::vector<switchblade::analysis::Transient> transients)
    {
        file_ = std::move (file);
        voiceBank_.stopAll();

        for (auto& p : pads_)
        {
            p.start = -1;
            p.end   = -1;
            p.flash = 0.0f;
        }

        if (! file_ || transients.empty())
        {
            repaint();
            return;
        }

        // Slice to natural energy-decay end (full ADSR preserved).
        // Slices may overlap — each pad is an independent one-shot.
        // Fall back to next-onset boundary only when naturalEnd is not set.
        const juce::int64 fileLen = file_->samples.getNumSamples();
        const std::size_t numSlices = std::min (
            static_cast<std::size_t> (kNumPads), transients.size());

        for (std::size_t i = 0; i < numSlices; ++i)
        {
            const juce::int64 start = transients[i].sampleIndex;
            const juce::int64 end   = (transients[i].naturalEnd > 0)
                ? transients[i].naturalEnd
                : ((i + 1 < transients.size())
                    ? transients[i + 1].sampleIndex
                    : fileLen);
            pads_[i].start = start;
            pads_[i].end   = std::min (end, fileLen);
        }
        repaint();
    }

    void PreviewGrid::clear() noexcept
    {
        file_.reset();
        voiceBank_.stopAll();
        for (auto& p : pads_) { p.start = -1; p.end = -1; p.flash = 0.0f; }
        repaint();
    }

    //==========================================================================
    //  Trigger
    //==========================================================================
    void PreviewGrid::triggerPad (int index)
    {
        if (index < 0 || index >= kNumPads)
            return;
        auto& pad = pads_[static_cast<std::size_t> (index)];
        if (pad.start < 0 || ! file_)
            return;

        voiceBank_.trigger (file_, pad.start, pad.end);
        pad.flash = 1.0f;   // Full intensity neon pulse
        repaint (padBounds (index));
    }

    //==========================================================================
    //  Painting
    //==========================================================================
    juce::Rectangle<int> PreviewGrid::padBounds (int index) const
    {
        const int row  = index / kCols;
        const int col  = index % kCols;
        const int padW = getWidth()  / kCols;
        const int padH = getHeight() / kRows;
        return { col * padW, row * padH, padW, padH };
    }

    int PreviewGrid::padAt (juce::Point<int> p) const
    {
        const int col = p.x / (getWidth()  / kCols);
        const int row = p.y / (getHeight() / kRows);
        if (col < 0 || col >= kCols || row < 0 || row >= kRows)
            return -1;
        return row * kCols + col;
    }

    void PreviewGrid::paint (juce::Graphics& g)
    {
        g.fillAll (pal::ChromeVoid);

        for (int i = 0; i < kNumPads; ++i)
        {
            const auto& pad    = pads_[static_cast<std::size_t> (i)];
            const auto  bounds = padBounds (i).reduced (3);
            const bool  loaded = pad.start >= 0;
            const float flash  = pad.flash;

            // Chrome bevel
            {
                juce::Path p;
                p.addRoundedRectangle (bounds.toFloat(), 6.0f);
                g.setGradientFill (pal::chromeBevel (bounds.toFloat(), false));
                g.fillPath (p);
            }

            // Body tint — stronger when flash active
            {
                const auto neon = pal::NeonCyan;
                const auto body = bounds.reduced (2);
                juce::Path bp;
                bp.addRoundedRectangle (body.toFloat(), 4.0f);

                if (flash > 0.0f)
                {
                    // Neon pulse: 0.15 base + up to 0.7 alpha at full flash
                    g.setColour (neon.withAlpha (0.15f + flash * 0.70f));
                }
                else if (loaded)
                {
                    g.setColour (pal::GlassTintDeep);
                }
                else
                {
                    g.setColour (pal::ChromeVoid.withAlpha (0.8f));
                }
                g.fillPath (bp);
            }

            // Neon glow border — bright pulse on hit, subtle when loaded
            if (flash > 0.0f)
            {
                // Multi-ring glow for tactile feedback
                for (int i = 3; i >= 1; --i)
                {
                    const float ringAlpha = flash * 0.4f / static_cast<float> (i);
                    const float ringWidth = 1.0f + static_cast<float> (i) * 0.5f;
                    g.setColour (pal::NeonCyan.withAlpha (ringAlpha));
                    g.drawRoundedRectangle (
                        bounds.expanded (static_cast<float> (i)).toFloat(),
                        6.0f + static_cast<float> (i),
                        ringWidth);
                }
            }
            else if (loaded)
            {
                g.setColour (pal::ChromeMid);
                g.drawRoundedRectangle (bounds.toFloat(), 6.0f, 1.0f);
            }

            // Key label + number
            g.setFont (juce::Font (juce::FontOptions {
                static_cast<float> (bounds.getHeight()) * 0.32f }).boldened());
            g.setColour (loaded ? (flash > 0.0f ? pal::NeonCyan : pal::TextPrimary)
                                : pal::TextDisabled);
            g.drawFittedText (pad.label, bounds,
                              juce::Justification::centred, 1);

            // Pad index below label (smaller)
            g.setFont (juce::Font (juce::FontOptions {
                static_cast<float> (bounds.getHeight()) * 0.18f }));
            g.setColour (loaded ? pal::TextSecondary : pal::TextDisabled);
            g.drawFittedText (juce::String (i + 1),
                              bounds.withTrimmedTop (bounds.getHeight() / 2),
                              juce::Justification::centred, 1);
        }
    }

    void PreviewGrid::resized()
    {
        repaint();
    }

    //==========================================================================
    //  Input
    //==========================================================================
    void PreviewGrid::mouseDown (const juce::MouseEvent& e)
    {
        triggerPad (padAt (e.getPosition()));
        // Grab keyboard focus so 1-4 / Q-R keys work immediately after clicking
        grabKeyboardFocus();
    }

    bool PreviewGrid::keyPressed (const juce::KeyPress& k, juce::Component*)
    {
        const auto ch = static_cast<char> (
            juce::CharacterFunctions::toUpperCase (
                static_cast<juce::juce_wchar> (k.getTextCharacter())));

        for (int i = 0; i < kNumPads; ++i)
        {
            if (kKeyMap[static_cast<std::size_t> (i)] == ch)
            {
                triggerPad (i);
                return true;
            }
        }
        return false;
    }

    //==========================================================================
    //  Flash decay timer
    //==========================================================================
    void PreviewGrid::timerCallback()
    {
        bool anyFlash = false;
        for (int i = 0; i < kNumPads; ++i)
        {
            auto& f = pads_[static_cast<std::size_t> (i)].flash;
            if (f > 0.0f)
            {
                f = std::max (0.0f, f - 0.08f);   // ~375 ms full decay — snappy, tactile
                repaint (padBounds (i));
                anyFlash = true;
            }
        }
        juce::ignoreUnused (anyFlash);
    }
} // namespace switchblade::ui
