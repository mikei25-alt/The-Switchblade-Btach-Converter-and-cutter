#include "UI/MainContainer.h"
#include "UI/SwitchbladeLookAndFeel.h"
#include "Core/Palette.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include "BinaryData.h"

// =============================================================================
//  SwitchbladeApp — JUCE application entry point.
//
//  Responsibilities:
//    1. Install the Neon-Deco LookAndFeel globally before any Components exist.
//    2. Create a DocumentWindow and attach an OpenGLContext (for bloom/glass).
//    3. Tear everything down cleanly on quit.
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
    //  MainWindow — DocumentWindow that hosts MainContainer + OpenGL context.
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
            setResizeLimits (800, 500, 3840, 2160);

            container_ = std::make_unique<switchblade::ui::MainContainer>();
            container_->initAudioDevice();

            // OpenGL context — attaches to this window; renders on a background
            // GL thread. Bloom and frosted-glass compositing happen here.
            glContext_.setOpenGLVersionRequired (
                juce::OpenGLContext::openGL3_2);
            glContext_.attachTo (*this);

            setContentNonOwned (container_.get(), true);
            centreWithSize (1280, 800);
            setVisible (true);

            // Set taskbar / title-bar icon from the embedded PNG asset.
            const auto icon = juce::ImageCache::getFromMemory (
                BinaryData::logo_png, BinaryData::logo_pngSize);
            if (! icon.isNull())
                setIcon (icon);
        }

        ~MainWindow() override
        {
            glContext_.detach();
        }

        void closeButtonPressed() override
        {
            app_.systemRequestedQuit();
        }

    private:
        SwitchbladeApp&                                    app_;
        std::unique_ptr<switchblade::ui::MainContainer>    container_;
        juce::OpenGLContext                                glContext_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow>                                window_;
    std::unique_ptr<switchblade::ui::SwitchbladeLookAndFeel>   laf_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwitchbladeApp)
};

START_JUCE_APPLICATION (SwitchbladeApp)
