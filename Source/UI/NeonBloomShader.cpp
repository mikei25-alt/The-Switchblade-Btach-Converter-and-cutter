#include "NeonBloomShader.h"
#include "Shaders.h"

namespace switchblade::ui
{
    NeonBloomShader::NeonBloomShader (juce::OpenGLContext& context)
        : glContext_ (context) {}

    bool NeonBloomShader::compileIfNeeded()
    {
        if (ready_)
            return true;

        program_ = std::make_unique<juce::OpenGLShaderProgram> (glContext_);

        const juce::String vs { shaders::kPassthroughVertex.data(),
                                shaders::kPassthroughVertex.size() };
        const juce::String fs { shaders::kNeonBloomFragment.data(),
                                shaders::kNeonBloomFragment.size() };

        if (! (program_->addVertexShader (vs)
               && program_->addFragmentShader (fs)
               && program_->link()))
        {
            DBG ("NeonBloomShader: compile/link failed: " << program_->getLastError());
            program_.reset();
            return false;
        }

        uSource_    = std::make_unique<juce::OpenGLShaderProgram::Uniform> (*program_, "uSource");
        uTexelSize_ = std::make_unique<juce::OpenGLShaderProgram::Uniform> (*program_, "uTexelSize");
        uDirection_ = std::make_unique<juce::OpenGLShaderProgram::Uniform> (*program_, "uDirection");
        uThreshold_ = std::make_unique<juce::OpenGLShaderProgram::Uniform> (*program_, "uThreshold");
        uIntensity_ = std::make_unique<juce::OpenGLShaderProgram::Uniform> (*program_, "uIntensity");
        uTint_      = std::make_unique<juce::OpenGLShaderProgram::Uniform> (*program_, "uTint");

        ready_ = true;
        return true;
    }

    void NeonBloomShader::bind (float texelW, float texelH,
                                bool horizontalPass,
                                float threshold,
                                float intensity,
                                juce::Colour tint)
    {
        if (! ready_ || program_ == nullptr)
            return;

        program_->use();

        if (uSource_)    uSource_->set (0);
        if (uTexelSize_) uTexelSize_->set (texelW, texelH);
        if (uDirection_) uDirection_->set (horizontalPass ? 1.0f : 0.0f,
                                           horizontalPass ? 0.0f : 1.0f);
        if (uThreshold_) uThreshold_->set (threshold);
        if (uIntensity_) uIntensity_->set (intensity);
        if (uTint_)      uTint_->set (tint.getFloatRed(),
                                      tint.getFloatGreen(),
                                      tint.getFloatBlue());
    }

    void NeonBloomShader::release()
    {
        ready_ = false;
        uSource_.reset();
        uTexelSize_.reset();
        uDirection_.reset();
        uThreshold_.reset();
        uIntensity_.reset();
        uTint_.reset();
        program_.reset();
    }
} // namespace switchblade::ui
