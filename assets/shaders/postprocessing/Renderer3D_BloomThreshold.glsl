#type vertex
#version 450

layout(location = 0) out vec2 v_uv;

// Fullscreen triangle — no vertex buffer needed.
// gl_VertexIndex 0,1,2 produce a triangle that covers the entire NDC clip space.
void main() {
    vec2 positions[3] = vec2[3](
            vec2(-1.0, -1.0),
            vec2( 3.0, -1.0),
            vec2(-1.0,  3.0)
    );
    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);
    // NDC [-1,1] -> UV [0,1]. Vulkan NDC Y points down, texture V also increases down.
    v_uv = pos * 0.5 + 0.5;
}

#type fragment
#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set=1, binding=0) uniform BloomParamsUBO {
    float threshold;
    float soft_knee;
    float strength;
    int _pad;
} u_BloomParams;

layout(set=1, binding=1) uniform texture2D u_HDRColor;
layout(set=1, binding=2) uniform sampler   u_LinearSampler;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 sample_hdr(vec2 uv) {
    return texture(sampler2D(u_HDRColor, u_LinearSampler), uv).rgb;
}

void main() {
    // 5-tap Kawase downsample: this pass reads hdrColor at full res and writes
    // bloomMip0 at half res, so the offset is half a *source* texel — sampling the
    // center plus 4 diagonal corners of the 2x2 source block this destination pixel
    // covers. Weighted 4:1:1:1:1 (sum 8) instead of a plain unweighted average: this
    // is the specific weighting that makes dual-Kawase behave like a Gaussian-ish
    // blur across the whole mip chain instead of a blocky box blur.
    vec2 src_texel = 1.0 / vec2(textureSize(sampler2D(u_HDRColor, u_LinearSampler), 0));
    vec2 halfpixel = src_texel * 0.5;

    vec3 sum = sample_hdr(v_uv) * 4.0;
    sum += sample_hdr(v_uv - halfpixel);
    sum += sample_hdr(v_uv + halfpixel);
    sum += sample_hdr(v_uv + vec2(halfpixel.x, -halfpixel.y));
    sum += sample_hdr(v_uv - vec2(halfpixel.x, -halfpixel.y));
    vec3 color = sum / 8.0;

    // Karis soft-knee threshold. A hard cutoff (color = luma > threshold ? color : 0)
    // flickers: as a pixel's luminance crosses the threshold frame to frame (camera
    // motion, TAA jitter later on), it pops fully in/out of the bloom instead of fading.
    // The knee band [threshold-knee, threshold+knee] eases the transition with a smooth
    // quadratic instead of a step.
    float brightness = luminance(color);
    float knee = max(u_BloomParams.soft_knee, 1e-4);
    float soft = clamp(brightness - u_BloomParams.threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float contribution = max(soft, brightness - u_BloomParams.threshold);
    contribution /= max(brightness, 1e-5);

    o_color = vec4(color * contribution, 1.0);
}