#pragma once

#include <juce_opengl/juce_opengl.h>

namespace switchblade::ui
{
    //==========================================================================
    //  NeonBloomShader
    //
    //  Owns the two-pass (horizontal → vertical) separable Gaussian bloom
    //  shader used by the main window compositor to glow neon signal elements
    //  (waveforms, transient markers, active toggles, focus rings).
    //
    //  This is a *resource owner*, not a Component. The main window's GL
    //  renderer calls compileIfNeeded() on first paint, then bind() before
    //  drawing its fullscreen quad. The shader samples from the scene FBO
    //  and outputs to the back buffer.
    //==========================================================================
    class NeonBloomShader
    {
    public:
        explicit NeonBloomShader (juce::OpenGLContext& context);
        ~NeonBloomShader() = default;

        NeonBloomShader (const NeonBloomShader&) = delete;
        NeonBloomShader& operator= (const NeonBloomShader&) = delete;

        /** Compiles on first call after a context is (re)created. Must be
            invoked on the GL thread. Returns true if shader is usable. */
        bool compileIfNeeded();

        /** Bind program + upload per-frame uniforms. Call on GL thread. */
        void bind (float texelW, float texelH,
                   bool horizontalPass,
                   float threshold,
                   float intensity,
                   juce::Colour tint);

        /** Release all GPU resources. Call from openGLContextClosing(). */
        void release();

        [[nodiscard]] bool isReady() const noexcept { return ready_; }

    private:
        juce::OpenGLContext& glContext_;
        std::unique_ptr<juce::OpenGLShaderProgram> program_;

        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uSource_;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uTexelSize_;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uDirection_;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uThreshold_;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uIntensity_;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uTint_;

        bool ready_ { false };

        JUCE_LEAK_DETECTOR (NeonBloomShader)
    };
} // namespace switchblade::ui
