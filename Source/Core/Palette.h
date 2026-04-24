#pragma once

#include <juce_graphics/juce_graphics.h>

// =============================================================================
//  Palette.h — Neon-Deco colour system for The Switchblade.
//  Organized by role: chrome (structural), glass (surfaces), neon (signal).
//  Use the role-based aliases in painting code; edit hex values here only.
// =============================================================================

namespace switchblade::palette
{
    // Chrome — structural bezels, rails, borders
    inline const juce::Colour ChromeVoid     { 0xFF0B0D12 };  // darkest shadow
    inline const juce::Colour ChromeDark     { 0xFF181A20 };
    inline const juce::Colour ChromeMid      { 0xFF3A3F4A };
    inline const juce::Colour ChromeLight    { 0xFFB8BEC9 };
    inline const juce::Colour ChromeHigh     { 0xFFE8ECF2 };  // highlight rim
    inline const juce::Colour ChromeSpec     { 0xFFFFFFFF };  // hot spec

    // Frosted glass — card bodies, panels
    inline const juce::Colour GlassTint      { 0xB0182230 };   // semi-transparent tint
    inline const juce::Colour GlassTintDeep  { 0xC0151A22 };
    inline const juce::Colour GlassRim       { 0x1AFFFFFF };  // inner highlight
    inline const juce::Colour GlassShade     { 0x60000000 };  // drop shadow

    // Neon — signal / state / glow
    inline const juce::Colour NeonCyan       { 0xFF5CF3FF };  // waveform
    inline const juce::Colour NeonGold       { 0xFFFFC84A };  // transient markers
    inline const juce::Colour NeonMagenta    { 0xFFFF3EA5 };  // melodic / pitch
    inline const juce::Colour NeonMint       { 0xFF63FFB4 };  // success / active
    inline const juce::Colour NeonCrimson    { 0xFFFF4860 };  // danger / clip

    // Typography
    inline const juce::Colour TextPrimary    { 0xFFE8ECF2 };
    inline const juce::Colour TextSecondary  { 0xFF9AA2B1 };
    inline const juce::Colour TextDisabled   { 0xFF4A5160 };

    // Derived helpers -------------------------------------------------------
    [[nodiscard]] inline juce::Colour neonFor (int accentIndex) noexcept
    {
        switch (accentIndex & 3)
        {
            case 0:  return NeonCyan;
            case 1:  return NeonGold;
            case 2:  return NeonMagenta;
            default: return NeonMint;
        }
    }

    [[nodiscard]] inline juce::ColourGradient chromeBevel (juce::Rectangle<float> r, bool pressed = false) noexcept
    {
        juce::ColourGradient g { pressed ? ChromeDark : ChromeHigh,
                                 r.getX(), r.getY(),
                                 pressed ? ChromeHigh : ChromeDark,
                                 r.getX(), r.getBottom(),
                                 false };
        g.addColour (0.35, ChromeLight);
        g.addColour (0.65, ChromeMid);
        return g;
    }
} // namespace switchblade::palette
