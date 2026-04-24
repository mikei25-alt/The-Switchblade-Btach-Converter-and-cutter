#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>

namespace switchblade::ui
{
    //==========================================================================
    //  FrostedGlassPanel
    //
    //  A Component that participates in an OpenGL render pipeline to produce
    //  a proper depth-of-field frosted glass effect behind its children. The
    //  panel captures the backdrop (parent texture) and runs FrostedGlass.glsl
    //  across its bounds, then JUCE composites child components on top.
    //
    //  Graceful degradation: if the supplied OpenGLContext is not active, the
    //  panel renders the CPU fallback (translucent tint via LookAndFeel), so
    //  it can be used in any parent without branching.
    //
    //  VBlankAttachment drives a 60fps repaint when animated (e.g. during
    //  hover transitions) without burning CPU when idle.
    //==========================================================================
    class FrostedGlassPanel : public juce::Component,
                              private juce::OpenGLRenderer
    {
    public:
        explicit FrostedGlassPanel (juce::OpenGLContext& sharedContext);
        ~FrostedGlassPanel() override;

        FrostedGlassPanel (const FrostedGlassPanel&) = delete;
        FrostedGlassPanel& operator= (const FrostedGlassPanel&) = delete;

        /** Tint applied over the blurred backdrop. Alpha channel = strength. */
        void setGlassTint (juce::Colour tint) noexcept;

        /** Blur radius in texels (1.0 .. 8.0). */
        void setBlurRadius (float radius) noexcept;

        /** Corner radius in pixels. */
        void setCornerRadius (float radius) noexcept;

        //----- Component --------------------------------------------------------
        void paint (juce::Graphics&) override;
        void resized() override;

        //----- OpenGLRenderer ---------------------------------------------------
        void newOpenGLContextCreated() override;
        void renderOpenGL() override;
        void openGLContextClosing() override;

    private:
        juce::OpenGLContext& glContext_;
        std::unique_ptr<juce::OpenGLShaderProgram> shader_;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uBackdrop_;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uTexelSize_;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uBlurRadius_;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uTint_;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uRim_;

        juce::Colour  tint_         { 0xB0182230 };
        float         blurRadius_   { 3.5f };
        float         cornerRadius_ { 8.0f };

        std::atomic<bool> shadersReady_ { false };

        juce::VBlankAttachment vblank_;

        JUCE_LEAK_DETECTOR (FrostedGlassPanel)
    };
} // namespace switchblade::ui
