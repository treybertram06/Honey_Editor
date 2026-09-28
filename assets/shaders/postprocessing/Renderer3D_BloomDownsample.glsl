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

// Generic single-input downsample: whichever bloomMip[i-1] the .hnfg pass binds here
// via `Shader: u_Source`. Same shader/pipeline reused for all 5 downsample passes.
layout(set=1, binding=0) uniform texture2D u_Source;
layout(set=1, binding=1) uniform sampler   u_LinearSampler;

vec3 sample_src(vec2 uv) {
    return texture(sampler2D(u_Source, u_LinearSampler), uv).rgb;
}

void main() {
    vec2 src_texel = 1.0 / vec2(textureSize(sampler2D(u_Source, u_LinearSampler), 0));
    vec2 halfpixel = src_texel * 0.5;

    vec3 sum = sample_src(v_uv) * 4.0;
    sum += sample_src(v_uv - halfpixel);
    sum += sample_src(v_uv + halfpixel);
    sum += sample_src(v_uv + vec2(halfpixel.x, -halfpixel.y));
    sum += sample_src(v_uv - vec2(halfpixel.x, -halfpixel.y));

    o_color = vec4(sum / 8.0, 1.0);
}