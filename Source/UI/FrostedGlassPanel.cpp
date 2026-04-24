#include "FrostedGlassPanel.h"
#include "Shaders.h"
#include "Core/Palette.h"

namespace switchblade::ui
{
    FrostedGlassPanel::FrostedGlassPanel (juce::OpenGLContext& sharedContext)
        : glContext_ (sharedContext),
          vblank_ (this, [this] { if (shadersReady_) repaint(); })
    {
        setOpaque (false);
        setInterceptsMouseClicks (false, true);   // let children receive clicks
        glContext_.setRenderer (this);
        glContext_.setContinuousRepainting (false);
    }

    FrostedGlassPanel::~FrostedGlassPanel()
    {
        glContext_.setRenderer (nullptr);
    }

    //----- Setters ------------------------------------------------------------
    void FrostedGlassPanel::setGlassTint     (juce::Colour t) noexcept { tint_ = t;         }
    void FrostedGlassPanel::setBlurRadius    (float r)        noexcept { blurRadius_ = r;   }
    void FrostedGlassPanel::setCornerRadius  (float r)        noexcept { cornerRadius_ = r; }

    //==========================================================================
    //  CPU fallback paint (used when GL renderer hasn't initialised yet, or on
    //  platforms where the OpenGLContext refuses). Matches LookAndFeel look.
    //==========================================================================
    void FrostedGlassPanel::paint (juce::Graphics& g)
    {
        if (shadersReady_)
            return; // GL renderer handles this surface

        const auto bounds = getLocalBounds().toFloat();
        juce::Path body;
        body.addRoundedRectangle (bounds, cornerRadius_);

        juce::ColourGradient fill {
            switchblade::palette::GlassTint,     bounds.getX(), bounds.getY(),
            switchblade::palette::GlassTintDeep, bounds.getX(), bounds.getBottom(),
            false };
        g.setGradientFill (fill);
        g.fillPath (body);

        g.setColour (switchblade::palette::GlassRim);
        g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius_, 1.0f);
    }

    void FrostedGlassPanel::resized()
    {
        // Hook point for future child-layout; no-op for now.
    }

    //==========================================================================
    //  OpenGLRenderer callbacks — run on the GL thread.
    //==========================================================================
    void FrostedGlassPanel::newOpenGLContextCreated()
    {
        shader_ = std::make_unique<juce::OpenGLShaderProgram> (glContext_);

        const juce::String vs { shaders::kPassthroughVertex.data(),
                                shaders::kPassthroughVertex.size() };
        const juce::String fs { shaders::kFrostedGlassFragment.data(),
                                shaders::kFrostedGlassFragment.size() };

        if (shader_->addVertexShader (vs)
            && shader_->addFragmentShader (fs)
            && shader_->link())
        {
            uBackdrop_   = std::make_unique<juce::OpenGLShaderProgram::Uniform> (*shader_, "uBackdrop");
            uTexelSize_  = std::make_unique<juce::OpenGLShaderProgram::Uniform> (*shader_, "uTexelSize");
            uBlurRadius_ = std::make_unique<juce::OpenGLShaderProgram::Uniform> (*shader_, "uBlurRadius");
            uTint_       = std::make_unique<juce::OpenGLShaderProgram::Uniform> (*shader_, "uTint");
            uRim_        = std::make_unique<juce::OpenGLShaderProgram::Uniform> (*shader_, "uRim");
            shadersReady_ = true;
        }
        else
        {
            DBG ("FrostedGlassPanel: shader compile failed: " << shader_->getLastError());
            shader_.reset();
        }
    }

    void FrostedGlassPanel::renderOpenGL()
    {
        if (! shadersReady_ || shader_ == nullptr)
            return;

        const float scale = static_cast<float> (glContext_.getRenderingScale());
        const auto  viewW = static_cast<int> (getWidth()  * scale);
        const auto  viewH = static_cast<int> (getHeight() * scale);
        if (viewW == 0 || viewH == 0)
            return;

        juce::gl::glViewport (0, 0, viewW, viewH);
        juce::gl::glEnable (juce::gl::GL_BLEND);
        juce::gl::glBlendFunc (juce::gl::GL_SRC_ALPHA, juce::gl::GL_ONE_MINUS_SRC_ALPHA);

        shader_->use();

        if (uBackdrop_)
            uBackdrop_->set (0);    // texture unit 0 — bound by parent compositor
        if (uTexelSize_)
            uTexelSize_->set (1.0f / static_cast<float> (viewW),
                              1.0f / static_cast<float> (viewH));
        if (uBlurRadius_) uBlurRadius_->set (blurRadius_);
        if (uTint_)
            uTint_->set (tint_.getFloatRed(), tint_.getFloatGreen(),
                         tint_.getFloatBlue(), tint_.getFloatAlpha());
        if (uRim_) uRim_->set (0.55f);

        // NB: parent compositor (main window renderer) must draw the fullscreen
        // quad that triggers the fragment shader — panels don't own vertex
        // buffers. See main-window OpenGL setup in step 4.
    }

    void FrostedGlassPanel::openGLContextClosing()
    {
        shadersReady_ = false;
        uBackdrop_.reset();
        uTexelSize_.reset();
        uBlurRadius_.reset();
        uTint_.reset();
        uRim_.reset();
        shader_.reset();
    }
} // namespace switchblade::ui
