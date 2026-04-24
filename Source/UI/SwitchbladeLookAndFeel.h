#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace switchblade::ui
{
    //==========================================================================
    //  SwitchbladeLookAndFeel
    //
    //  Global Neon-Deco look. Derives from LookAndFeel_V4 so we inherit modern
    //  JUCE defaults, then overrides only the surfaces that define the
    //  "3D chrome + frosted glass + neon signal" identity.
    //
    //  Usage:
    //      auto laf = std::make_unique<switchblade::ui::SwitchbladeLookAndFeel>();
    //      juce::LookAndFeel::setDefaultLookAndFeel (laf.get());
    //
    //  All colours live in switchblade::palette — do not hardcode hex here.
    //==========================================================================
    class SwitchbladeLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        SwitchbladeLookAndFeel();
        ~SwitchbladeLookAndFeel() override = default;

        SwitchbladeLookAndFeel (const SwitchbladeLookAndFeel&) = delete;
        SwitchbladeLookAndFeel& operator= (const SwitchbladeLookAndFeel&) = delete;

        //----- Buttons -------------------------------------------------------
        void drawButtonBackground (juce::Graphics&,
                                   juce::Button&,
                                   const juce::Colour& backgroundColour,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown) override;

        void drawButtonText (juce::Graphics&,
                             juce::TextButton&,
                             bool shouldDrawButtonAsHighlighted,
                             bool shouldDrawButtonAsDown) override;

        //----- The Switchblade toggle (3D mechanical throw-switch) ------------
        void drawToggleButton (juce::Graphics&,
                               juce::ToggleButton&,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

        //----- Sliders -------------------------------------------------------
        void drawRotarySlider (juce::Graphics&,
                               int x, int y, int w, int h,
                               float sliderPosProportional,
                               float rotaryStartAngle,
                               float rotaryEndAngle,
                               juce::Slider&) override;

        void drawLinearSlider (juce::Graphics&,
                               int x, int y, int w, int h,
                               float sliderPos,
                               float minSliderPos,
                               float maxSliderPos,
                               const juce::Slider::SliderStyle,
                               juce::Slider&) override;

        //----- Combos / text -------------------------------------------------
        void drawComboBox (juce::Graphics&,
                           int width, int height,
                           bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH,
                           juce::ComboBox&) override;

        void fillTextEditorBackground (juce::Graphics&,
                                       int width, int height,
                                       juce::TextEditor&) override;

        void drawTextEditorOutline (juce::Graphics&,
                                    int width, int height,
                                    juce::TextEditor&) override;

        //----- Typography ----------------------------------------------------
        juce::Font getTextButtonFont       (juce::TextButton&, int buttonHeight) override;
        juce::Font getLabelFont            (juce::Label&) override;
        juce::Font getComboBoxFont         (juce::ComboBox&) override;

    private:
        // Shared painting primitives ------------------------------------------
        void paintChromeBevel (juce::Graphics&,
                               juce::Rectangle<float> bounds,
                               float cornerRadius,
                               bool pressed) const;

        void paintFrostedBody (juce::Graphics&,
                               juce::Rectangle<float> bounds,
                               float cornerRadius) const;

        void paintNeonGlow (juce::Graphics&,
                            juce::Rectangle<float> bounds,
                            juce::Colour neon,
                            float intensity,
                            float cornerRadius) const;

        juce::Typeface::Ptr displayTypeface_;  // Art-Deco display face
        juce::Typeface::Ptr bodyTypeface_;     // Clean mono for readings

        JUCE_LEAK_DETECTOR (SwitchbladeLookAndFeel)
    };
} // namespace switchblade::ui
