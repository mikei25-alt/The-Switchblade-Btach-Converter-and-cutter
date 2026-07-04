#include "UI/MainContainer.h"
#include "UI/SwitchbladeLookAndFeel.h"
#include "Core/Palette.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include "BinaryData.h"

// =============================================================================
//  SwitchbladeApp — JUCE application entry point.
//
//  Responsibilities:
//    1. Install the Neon-Deco LookAndFeel globally before any Components exist.
//    2. Create the DocumentWindow hosting MainContainer.
//    3. Tear everything down cleanly on quit.
//
//  Rendering is plain JUCE software compositing — the bloom/glass effects are
//  painted per-component (see Palette / LookAndFeel). An OpenGL context was
//  once attached here for a shader layer that never shipped; it forced GL
//  compositing of the whole tree for zero visual benefit, so it was removed.
// =============================================================================

class SwitchbladeApp final : public juce::JUCEApplication
{
public:
    SwitchbladeApp() = default;

    //==========================================================================
    //  JUCEApplication
    //==========================================================================
    const juce::String getApplicationName()    override { return "The Switchblade"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed()          override { return false; }

    void initialise (const juce::String& /*commandLine*/) override
    {
        laf_ = std::make_unique<switchblade::ui::SwitchbladeLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (laf_.get());

        window_ = std::make_unique<MainWindow> (*this);
    }

    void shutdown() override
    {
        window_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        laf_.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String& /*commandLine*/) override {}

private:
    //==========================================================================
    //  MainWindow — DocumentWindow that hosts MainContainer.
    //==========================================================================
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (SwitchbladeApp& app)
            : juce::DocumentWindow (
                  "The Switchblade",
                  switchblade::palette::ChromeVoid,
                  juce::DocumentWindow::allButtons),
              app_ (app)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, false);
            // 1000px is the narrowest the top bar survives with its controls
            // squeezed to their 55% floor (Grid mode adds BPM + Max fields);
            // 560px keeps the preview grid + vault both usable.
            setResizeLimits (1000, 560, 3840, 2160);

            container_ = std::make_unique<switchblade::ui::MainContainer>();
            container_->initAudioDevice();

            setContentNonOwned (container_.get(), true);
            centreWithSize (1280, 800);
            setVisible (true);

            // Set taskbar / title-bar icon from the embedded PNG asset.
            const auto icon = juce::ImageCache::getFromMemory (
                BinaryData::logo_png, BinaryData::logo_pngSize);
            if (! icon.isNull())
                setIcon (icon);
        }

        void closeButtonPressed() override
        {
            app_.systemRequestedQuit();
        }

    private:
        SwitchbladeApp&                                    app_;
        std::unique_ptr<switchblade::ui::MainContainer>    container_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow>                                window_;
    std::unique_ptr<switchblade::ui::SwitchbladeLookAndFeel>   laf_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwitchbladeApp)
};

START_JUCE_APPLICATION (SwitchbladeApp)
