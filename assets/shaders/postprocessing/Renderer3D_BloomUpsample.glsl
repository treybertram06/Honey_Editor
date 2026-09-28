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

// u_Low  = the smaller, already-processed image being magnified back up (bloomMip5 for
//          the first upsample pass, bloomUp[i+1] for every subsequent one).
// u_Detail = the same-resolution mip from the downsample chain (bloomMip[i]), summed
//            back in so this level isn't purely a blurred copy of a smaller one — each
//            level keeps some of its own frequency content instead of the whole chain
//            converging to looking like the smallest mip blown up.
layout(set=1, binding=0) uniform texture2D u_Low;
layout(set=1, binding=1) uniform texture2D u_Detail;
layout(set=1, binding=2) uniform sampler   u_LinearSampler;

vec3 sample_low(vec2 uv) {
    return texture(sampler2D(u_Low, u_LinearSampler), uv).rgb;
}

void main() {
    // 8-tap tent upsample (dual-Kawase's upsample counterpart to the downsample's box
    // filter): 4 axis-aligned taps at 2x weight, 4 diagonal taps at 1x weight, sum 12.
    // halfpixel is relative to u_Low's resolution since that's the image being
    // magnified — not u_Detail's, which is already at this pass's output resolution.
    vec2 low_texel = 1.0 / vec2(textureSize(sampler2D(u_Low, u_LinearSampler), 0));
    vec2 halfpixel = low_texel * 0.5;

    vec3 sum  = sample_low(v_uv + vec2(-halfpixel.x * 2.0, 0.0));
    sum      += sample_low(v_uv + vec2(-halfpixel.x, halfpixel.y)) * 2.0;
    sum      += sample_low(v_uv + vec2(0.0, halfpixel.y * 2.0));
    sum      += sample_low(v_uv + vec2(halfpixel.x, halfpixel.y)) * 2.0;
    sum      += sample_low(v_uv + vec2(halfpixel.x * 2.0, 0.0));
    sum      += sample_low(v_uv + vec2(halfpixel.x, -halfpixel.y)) * 2.0;
    sum      += sample_low(v_uv + vec2(0.0, -halfpixel.y * 2.0));
    sum      += sample_low(v_uv + vec2(-halfpixel.x, -halfpixel.y)) * 2.0;
    vec3 upsampled = sum / 12.0;

    vec3 detail = texture(sampler2D(u_Detail, u_LinearSampler), v_uv).rgb;
    o_color = vec4(upsampled + detail, 1.0);
}