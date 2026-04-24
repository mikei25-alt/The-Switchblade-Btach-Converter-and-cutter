#pragma once

// =============================================================================
//  Shaders.h — Inline GLSL fragment shaders for the Neon-Deco pipeline.
//  Targeting OpenGL 3.2 core (JUCE's OpenGLShaderProgram portable profile).
//  Shaders are stored as string_view so they're zero-cost at link time.
// =============================================================================

#include <string_view>

namespace switchblade::shaders
{
    // -------------------------------------------------------------------------
    // Pass-through vertex shader. Emits UV in [0,1].
    // -------------------------------------------------------------------------
    inline constexpr std::string_view kPassthroughVertex = R"GLSL(
        #version 150
        in vec2 position;
        out vec2 vUV;
        void main()
        {
            vUV = position * 0.5 + 0.5;
            gl_Position = vec4(position, 0.0, 1.0);
        }
    )GLSL";

    // -------------------------------------------------------------------------
    // NeonBloom — bright-pass threshold + separable Gaussian blur + additive
    // composite. Run twice: once with uDirection=(1,0), once with (0,1).
    // -------------------------------------------------------------------------
    inline constexpr std::string_view kNeonBloomFragment = R"GLSL(
        #version 150
        in  vec2      vUV;
        out vec4      fragColour;

        uniform sampler2D uSource;
        uniform vec2      uTexelSize;
        uniform vec2      uDirection;   // (1,0) horizontal, (0,1) vertical
        uniform float     uThreshold;   // 0.4 .. 0.8 — keeps highlights only
        uniform float     uIntensity;   // 0.5 .. 2.0 — bloom strength
        uniform vec3      uTint;        // neon colour, e.g. cyan

        // 9-tap Gaussian weights (sigma ~= 2.0)
        const float kW[5] = float[5](0.227027, 0.194594, 0.121622, 0.054054, 0.016216);

        vec3 brightPass(vec3 c)
        {
            float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
            float soft = smoothstep(uThreshold, uThreshold + 0.1, lum);
            return c * soft;
        }

        void main()
        {
            vec3 centre = texture(uSource, vUV).rgb;
            vec3 accum  = brightPass(centre) * kW[0];

            for (int i = 1; i < 5; ++i)
            {
                vec2 offs = uDirection * uTexelSize * float(i);
                accum += brightPass(texture(uSource, vUV + offs).rgb) * kW[i];
                accum += brightPass(texture(uSource, vUV - offs).rgb) * kW[i];
            }

            vec3 bloom = accum * uIntensity * uTint;
            fragColour = vec4(centre + bloom, 1.0);
        }
    )GLSL";

    // -------------------------------------------------------------------------
    // FrostedGlass — downsampled blur + chromatic tint + rim highlight.
    // -------------------------------------------------------------------------
    inline constexpr std::string_view kFrostedGlassFragment = R"GLSL(
        #version 150
        in  vec2      vUV;
        out vec4      fragColour;

        uniform sampler2D uBackdrop;
        uniform vec2      uTexelSize;
        uniform float     uBlurRadius;  // in texels
        uniform vec4      uTint;        // glass colour + alpha
        uniform float     uRim;         // 0..1 highlight at top edge

        void main()
        {
            // 13-tap disc blur (cheap, subjective-quality "frosted" look)
            vec3 sum = vec3(0.0);
            const vec2 kDisc[13] = vec2[13](
                vec2( 0.0,  0.0),  vec2( 1.0,  0.0),  vec2(-1.0,  0.0),
                vec2( 0.0,  1.0),  vec2( 0.0, -1.0),  vec2( 0.7,  0.7),
                vec2(-0.7,  0.7),  vec2( 0.7, -0.7),  vec2(-0.7, -0.7),
                vec2( 1.4,  0.0),  vec2(-1.4,  0.0),  vec2( 0.0,  1.4),
                vec2( 0.0, -1.4)
            );

            for (int i = 0; i < 13; ++i)
                sum += texture(uBackdrop, vUV + kDisc[i] * uTexelSize * uBlurRadius).rgb;
            sum /= 13.0;

            // Blend backdrop with tint, add top rim highlight
            vec3 tinted = mix(sum, uTint.rgb, uTint.a);
            float rim = smoothstep(0.98, 1.0, 1.0 - vUV.y) * uRim;
            fragColour = vec4(tinted + vec3(rim), 1.0);
        }
    )GLSL";
} // namespace switchblade::shaders
