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
#include "global_bindings.glsli"
layout(set = HN_GLOBAL_SET, binding = HN_GBIND_CAMERA) uniform CameraUBO {
    mat4 u_ViewProjection;
    vec3 u_Position;
    float u_Exposure;
    mat4 u_InvViewProjection;
    mat4 u_View;
    mat4 u_Projection;
    mat4 u_InvProjection;
} u_Camera;

layout(set=1, binding=0) uniform texture2D  u_HDRColor;
layout(set=1, binding=1) uniform sampler    u_LinearSampler;
layout(set=1, binding=2) uniform itexture2D u_EntityId;
layout(set=1, binding=3) uniform sampler    u_NearestSampler;

layout(location=0) in vec2 v_uv;

layout(location=0) out vec4 o_color;
layout(location=1) out int  o_entity_id;

vec3 aces_tonemap(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 color = texture(sampler2D(u_HDRColor, u_LinearSampler), v_uv).rgb;
    // (bloom add, exposure, tonemap, gamma, color-grade LUT all land here in later steps)
    color *= u_Camera.u_Exposure;
    color  = aces_tonemap(color);
    color  = pow(color, vec3(1.0/2.2));
    o_color = vec4(color, 1.0);
    o_entity_id = texelFetch(isampler2D(u_EntityId, u_NearestSampler), ivec2(gl_FragCoord.xy), 0).r;
}