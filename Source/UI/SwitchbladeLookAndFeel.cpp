#include "SwitchbladeLookAndFeel.h"
#include "Core/Palette.h"

#include <numbers>

namespace switchblade::ui
{
    namespace pal = switchblade::palette;

    //==========================================================================
    //  Construction
    //==========================================================================
    SwitchbladeLookAndFeel::SwitchbladeLookAndFeel()
    {
        // Seed global scheme colours so any control we don't override inherits
        // the Neon-Deco palette rather than LookAndFeel_V4 defaults.
        setColour (juce::ResizableWindow::backgroundColourId, pal::ChromeVoid);
        setColour (juce::Label::textColourId,                 pal::TextPrimary);
        setColour (juce::Label::textWhenEditingColourId,      pal::NeonCyan);
        setColour (juce::TextButton::buttonColourId,          pal::ChromeMid);
        setColour (juce::TextButton::buttonOnColourId,        pal::NeonCyan.withAlpha (0.35f));
        setColour (juce::TextButton::textColourOnId,          pal::ChromeVoid);
        setColour (juce::TextButton::textColourOffId,         pal::TextPrimary);
        setColour (juce::ComboBox::backgroundColourId,        pal::GlassTintDeep);
        setColour (juce::ComboBox::textColourId,              pal::TextPrimary);
        setColour (juce::ComboBox::outlineColourId,           pal::ChromeMid);
        setColour (juce::ComboBox::arrowColourId,             pal::NeonCyan);
        setColour (juce::TextEditor::backgroundColourId,      pal::GlassTintDeep);
        setColour (juce::TextEditor::textColourId,            pal::TextPrimary);
        setColour (juce::TextEditor::highlightColourId,       pal::NeonCyan.withAlpha (0.25f));
        setColour (juce::TextEditor::outlineColourId,         pal::ChromeMid);
        setColour (juce::PopupMenu::backgroundColourId,       pal::GlassTintDeep);
        setColour (juce::PopupMenu::textColourId,             pal::TextPrimary);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, pal::NeonCyan.withAlpha (0.20f));
        setColour (juce::Slider::backgroundColourId,          pal::ChromeDark);
        setColour (juce::Slider::trackColourId,               pal::NeonCyan);
        setColour (juce::Slider::thumbColourId,               pal::ChromeHigh);
        setColour (juce::Slider::rotarySliderFillColourId,    pal::NeonCyan);
        setColour (juce::Slider::rotarySliderOutlineColourId, pal::ChromeMid);

        // displayTypeface_ / bodyTypeface_ are reserved for bundled Art-Deco /
        // monospace assets (via BinaryData in a future step). Default-null for
        // now; font methods below derive sizes from component height instead.
    }

    //==========================================================================
    //  Shared painting primitives
    //==========================================================================
    void SwitchbladeLookAndFeel::paintChromeBevel (juce::Graphics& g,
                                                   juce::Rectangle<float> bounds,
                                                   float cornerRadius,
                                                   bool pressed) const
    {
        // Outer drop-shadow pass
        juce::Path body;
        body.addRoundedRectangle (bounds, cornerRadius);

        {
            juce::DropShadow shadow { pal::GlassShade, pressed ? 2 : 8,
                                      { 0, pressed ? 1 : 3 } };
            shadow.drawForPath (g, body);
        }

        // Body: vertical chrome gradient
        g.setGradientFill (pal::chromeBevel (bounds, pressed));
        g.fillPath (body);

        // Top 1px highlight rim
        g.setColour (pressed ? pal::ChromeMid : pal::ChromeHigh.withAlpha (0.75f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);

        // Inner darker line for depth
        g.setColour (pal::ChromeVoid.withAlpha (0.6f));
        g.drawRoundedRectangle (bounds.reduced (1.5f), cornerRadius - 1.0f, 1.0f);
    }

    void SwitchbladeLookAndFeel::paintFrostedBody (juce::Graphics& g,
                                                   juce::Rectangle<float> bounds,
                                                   float cornerRadius) const
    {
        // Placeholder: solid translucent tint. Real frosted effect is rendered
        // via FrostedGlassPanel + NeonBloomShader on the GL layer; this CPU
        // fallback keeps the aesthetic coherent when GL is unavailable.
        juce::Path body;
        body.addRoundedRectangle (bounds, cornerRadius);

        juce::ColourGradient fill { pal::GlassTint,          bounds.getX(), bounds.getY(),
                                    pal::GlassTintDeep,      bounds.getX(), bounds.getBottom(),
                                    false };
        g.setGradientFill (fill);
        g.fillPath (body);

        // Inner rim highlight
        g.setColour (pal::GlassRim);
        g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);
    }

    void SwitchbladeLookAndFeel::paintNeonGlow (juce::Graphics& g,
                                                juce::Rectangle<float> bounds,
                                                juce::Colour neon,
                                                float intensity,
                                                float cornerRadius) const
    {
        // Simulated bloom via three decreasing-alpha strokes at increasing blur
        // radius (CPU fallback for when the GL bloom pass is offline).
        for (int i = 3; i >= 1; --i)
        {
            const float r = static_cast<float> (i) * 2.0f;
            g.setColour (neon.withAlpha (0.10f * intensity * static_cast<float> (i)));
            g.drawRoundedRectangle (bounds.expanded (r), cornerRadius + r, r);
        }
        g.setColour (neon.withAlpha (0.85f * intensity));
        g.drawRoundedRectangle (bounds, cornerRadius, 1.25f);
    }

    //==========================================================================
    //  Buttons
    //==========================================================================
    void SwitchbladeLookAndFeel::drawButtonBackground (juce::Graphics& g,
                                                       juce::Button& button,
                                                       const juce::Colour& /*backgroundColour*/,
                                                       bool shouldDrawButtonAsHighlighted,
                                                       bool shouldDrawButtonAsDown)
    {
        const auto b = button.getLocalBounds().toFloat().reduced (0.5f);
        constexpr float radius = 4.0f;
        const bool isExport = button.getButtonText().startsWith ("Export");

        // Fill gradient — gold tint for Export Selection, flat matte for others
        if (isExport)
        {
            const juce::Colour top    = shouldDrawButtonAsDown
                                      ? juce::Colour (0xFF0C0A00)
                                      : juce::Colour (0xFF181400);
            const juce::Colour bottom = shouldDrawButtonAsDown
                                      ? juce::Colour (0xFF181400)
                                      : juce::Colour (0xFF0C0A00);
            g.setGradientFill ({ top, b.getX(), b.getY(),
                                 bottom, b.getX(), b.getBottom(), false });
        }
        else
        {
            const juce::Colour top    = shouldDrawButtonAsDown
                                      ? juce::Colour (0xFF0F0F0F)
                                      : juce::Colour (0xFF1A1A1A);
            const juce::Colour bottom = shouldDrawButtonAsDown
                                      ? juce::Colour (0xFF1A1A1A)
                                      : juce::Colour (0xFF0F0F0F);
            g.setGradientFill ({ top, b.getX(), b.getY(),
                                 bottom, b.getX(), b.getBottom(), false });
        }
        g.fillRoundedRectangle (b, radius);

        // Border — cyan on hover, gold for Export at rest, #333 for others at rest
        if (shouldDrawButtonAsHighlighted)
            g.setColour (juce::Colour (0xFF00FBFF).withAlpha (0.75f));
        else if (isExport)
            g.setColour (pal::NeonGold.withAlpha (0.55f));
        else
            g.setColour (juce::Colour (0xFF333333));
        g.drawRoundedRectangle (b, radius, 1.0f);

        // Hover: thin cyan top accent line
        if (shouldDrawButtonAsHighlighted && ! shouldDrawButtonAsDown)
        {
            g.setColour (juce::Colour (0xFF00FBFF).withAlpha (0.45f));
            g.drawLine (b.getX() + radius, b.getY() + 0.5f,
                        b.getRight() - radius, b.getY() + 0.5f, 1.0f);
        }
    }

    void SwitchbladeLookAndFeel::drawButtonText (juce::Graphics& g,
                                                 juce::TextButton& button,
                                                 bool /*shouldDrawButtonAsHighlighted*/,
                                                 bool shouldDrawButtonAsDown)
    {
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.setColour (pal::TextPrimary.withAlpha (shouldDrawButtonAsDown ? 0.75f : 1.0f));
        g.drawFittedText (button.getButtonText(),
                          button.getLocalBounds(),
                          juce::Justification::centred, 1);
    }

    //==========================================================================
    //  Switchblade toggle — mechanical throw-switch, 3D
    //==========================================================================
    void SwitchbladeLookAndFeel::drawToggleButton (juce::Graphics& g,
                                                   juce::ToggleButton& button,
                                                   bool shouldDrawButtonAsHighlighted,
                                                   bool /*shouldDrawButtonAsDown*/)
    {
        auto fullBounds = button.getLocalBounds().toFloat();
        const float labelH   = juce::jmin (fullBounds.getHeight() * 0.35f, 16.0f);
        const auto switchArea = fullBounds.removeFromTop (fullBounds.getHeight() - labelH)
                                           .reduced (4.0f);

        // Outer chassis — chrome pill
        const float pillRadius = switchArea.getHeight() * 0.5f;
        paintChromeBevel (g, switchArea, pillRadius, false);

        // Inner channel (darker well)
        const auto channel = switchArea.reduced (4.0f);
        g.setColour (pal::ChromeVoid);
        g.fillRoundedRectangle (channel, channel.getHeight() * 0.5f);

        // Throw lever position
        const bool on = button.getToggleState();
        const float knobDia = channel.getHeight() - 2.0f;
        const float travel  = channel.getWidth() - knobDia - 2.0f;
        const float knobX   = channel.getX() + 1.0f + (on ? travel : 0.0f);
        const auto knob = juce::Rectangle<float> { knobX, channel.getY() + 1.0f,
                                                   knobDia, knobDia };

        // Knob shadow under
        {
            juce::Path p;
            p.addEllipse (knob);
            juce::DropShadow { pal::ChromeVoid, 6, { 0, 2 } }.drawForPath (g, p);
        }

        // Knob body
        g.setGradientFill ({ pal::ChromeHigh, knob.getX(), knob.getY(),
                             pal::ChromeMid,  knob.getX(), knob.getBottom(),
                             false });
        g.fillEllipse (knob);

        // Neon indicator channel strip
        const auto indicatorStrip = on ? channel.withTrimmedRight (knobDia * 0.5f)
                                       : channel.withTrimmedLeft  (knobDia * 0.5f);
        g.setColour ((on ? pal::NeonMint : pal::NeonCrimson).withAlpha (0.55f));
        g.fillRoundedRectangle (indicatorStrip.reduced (3.0f),
                                indicatorStrip.getHeight() * 0.35f);

        if (shouldDrawButtonAsHighlighted)
            paintNeonGlow (g, switchArea, on ? pal::NeonMint : pal::NeonCyan, 0.7f, pillRadius);

        // Label below
        if (button.getButtonText().isNotEmpty())
        {
            g.setColour (pal::TextSecondary);
            g.setFont (juce::Font (juce::FontOptions { labelH - 2.0f }).boldened());
            g.drawFittedText (button.getButtonText(),
                              fullBounds.toNearestInt(),
                              juce::Justification::centred, 1);
        }
    }

    //==========================================================================
    //  Sliders
    //==========================================================================
    void SwitchbladeLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                                   int x, int y, int w, int h,
                                                   float sliderPosProportional,
                                                   float rotaryStartAngle,
                                                   float rotaryEndAngle,
                                                   juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> { x, y, w, h }.toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();

        const auto outer = juce::Rectangle<float>::leftTopRightBottom (
            centre.x - radius, centre.y - radius,
            centre.x + radius, centre.y + radius);

        // Chrome ring
        paintChromeBevel (g, outer, radius, slider.isMouseOverOrDragging (true));

        // Track arc
        const float trackRadius = radius - 6.0f;
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (pal::ChromeVoid);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Neon fill arc
        const float angle = rotaryStartAngle + sliderPosProportional
                          * (rotaryEndAngle - rotaryStartAngle);
        juce::Path fill;
        fill.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                            rotaryStartAngle, angle, true);
        g.setColour (pal::NeonCyan);
        g.strokePath (fill, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // Glow on fill (cheap radial)
        g.setColour (pal::NeonCyan.withAlpha (0.35f));
        g.strokePath (fill, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // Pointer
        juce::Path pointer;
        const float pointerLen = trackRadius - 3.0f;
        const float pointerW   = 2.5f;
        pointer.addRectangle (-pointerW * 0.5f, -pointerLen, pointerW, pointerLen * 0.6f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle)
                                    .translated (centre));
        g.setColour (pal::ChromeHigh);
        g.fillPath (pointer);

        // Centre cap
        const auto cap = juce::Rectangle<float> { 0.0f, 0.0f, 8.0f, 8.0f }.withCentre (centre);
        g.setGradientFill ({ pal::ChromeHigh, cap.getX(), cap.getY(),
                             pal::ChromeDark, cap.getRight(), cap.getBottom(), false });
        g.fillEllipse (cap);
    }

    void SwitchbladeLookAndFeel::drawLinearSlider (juce::Graphics& g,
                                                   int x, int y, int w, int h,
                                                   float sliderPos,
                                                   float /*minSliderPos*/,
                                                   float /*maxSliderPos*/,
                                                   const juce::Slider::SliderStyle style,
                                                   juce::Slider& slider)
    {
        const bool horizontal = (style == juce::Slider::LinearHorizontal
                              || style == juce::Slider::LinearBar);
        const auto full = juce::Rectangle<float> (
            static_cast<float> (x), static_cast<float> (y),
            static_cast<float> (w), static_cast<float> (h));

        // Track channel
        auto track = horizontal
            ? full.withSizeKeepingCentre (full.getWidth(), 6.0f)
            : full.withSizeKeepingCentre (6.0f, full.getHeight());
        g.setColour (pal::ChromeVoid);
        g.fillRoundedRectangle (track, 3.0f);

        // Neon fill
        auto fill = track;
        if (horizontal)
            fill.setRight (sliderPos);
        else
            fill.setTop (sliderPos);
        g.setColour (pal::NeonCyan);
        g.fillRoundedRectangle (fill, 3.0f);

        // Thumb
        const float thumbDia = horizontal ? full.getHeight() * 0.8f : full.getWidth() * 0.8f;
        const auto thumb = horizontal
            ? juce::Rectangle<float> { sliderPos - thumbDia * 0.5f,
                                       full.getCentreY() - thumbDia * 0.5f,
                                       thumbDia, thumbDia }
            : juce::Rectangle<float> { full.getCentreX() - thumbDia * 0.5f,
                                       sliderPos - thumbDia * 0.5f,
                                       thumbDia, thumbDia };

        paintChromeBevel (g, thumb, thumbDia * 0.5f,
                          slider.isMouseOverOrDragging (true));

        // Active-state neon bloom — fill track and thumb glow when interacting
        if (slider.isMouseOverOrDragging (true))
        {
            g.setColour (pal::NeonCyan.withAlpha (0.18f));
            g.fillRoundedRectangle (
                horizontal ? fill.expanded (0.0f, 3.0f)
                           : fill.expanded (3.0f, 0.0f), 5.0f);

            paintNeonGlow (g, thumb.expanded (1.0f), pal::NeonCyan, 0.55f,
                           thumbDia * 0.5f + 1.0f);
        }
    }

    //==========================================================================
    //  ComboBox / TextEditor
    //==========================================================================
    void SwitchbladeLookAndFeel::drawComboBox (juce::Graphics& g,
                                               int width, int height,
                                               bool /*isButtonDown*/,
                                               int /*buttonX*/, int /*buttonY*/,
                                               int /*buttonW*/, int /*buttonH*/,
                                               juce::ComboBox& box)
    {
        auto bounds = juce::Rectangle<float> (0, 0,
            static_cast<float> (width), static_cast<float> (height)).reduced (1.0f);
        const float radius = 6.0f;

        paintFrostedBody (g, bounds, radius);
        g.setColour (pal::ChromeMid);
        g.drawRoundedRectangle (bounds, radius, 1.0f);

        if (box.isMouseOver (true))
            paintNeonGlow (g, bounds, pal::NeonCyan, 0.5f, radius);

        // Arrow
        const auto arrowArea = bounds.removeFromRight (static_cast<float> (height));
        juce::Path arrow;
        arrow.addTriangle (arrowArea.getCentreX() - 4.0f, arrowArea.getCentreY() - 2.0f,
                           arrowArea.getCentreX() + 4.0f, arrowArea.getCentreY() - 2.0f,
                           arrowArea.getCentreX(),        arrowArea.getCentreY() + 4.0f);
        g.setColour (pal::NeonCyan);
        g.fillPath (arrow);
    }

    void SwitchbladeLookAndFeel::fillTextEditorBackground (juce::Graphics& g,
                                                           int width, int height,
                                                           juce::TextEditor& /*editor*/)
    {
        paintFrostedBody (g, { 0.0f, 0.0f,
                               static_cast<float> (width),
                               static_cast<float> (height) }, 4.0f);
    }

    void SwitchbladeLookAndFeel::drawTextEditorOutline (juce::Graphics& g,
                                                        int width, int height,
                                                        juce::TextEditor& editor)
    {
        const auto bounds = juce::Rectangle<float> (0.5f, 0.5f,
            static_cast<float> (width) - 1.0f, static_cast<float> (height) - 1.0f);
        g.setColour (editor.hasKeyboardFocus (true) ? pal::NeonCyan : pal::ChromeMid);
        g.drawRoundedRectangle (bounds, 4.0f, editor.hasKeyboardFocus (true) ? 1.5f : 1.0f);
    }

    //==========================================================================
    //  Typography
    //==========================================================================
    juce::Font SwitchbladeLookAndFeel::getTextButtonFont (juce::TextButton&, int)
    {
        return juce::Font (juce::FontOptions { 12.5f }).boldened();
    }

    juce::Font SwitchbladeLookAndFeel::getLabelFont (juce::Label& label)
    {
        // Honor any explicit Label::setFont() call. The default LookAndFeel_V2 also
        // does this — earlier versions of this LookAndFeel returned a height-based
        // font, which silently overrode every setFont() in the codebase and caused
        // tiny header labels to render at ~36pt and clip.
        return label.getFont();
    }

    juce::Font SwitchbladeLookAndFeel::getComboBoxFont (juce::ComboBox& box)
    {
        return juce::Font (juce::FontOptions {
            juce::jmin (14.0f, static_cast<float> (box.getHeight()) * 0.55f) });
    }
} // namespace switchblade::ui
