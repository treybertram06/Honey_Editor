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
#include "postprocessing/fxaa.glsl"

layout(set=1, binding=0) uniform texture2D  u_LDRColor;
layout(set=1, binding=1) uniform sampler    u_LinearSampler;
layout(set=1, binding=2) uniform itexture2D u_EntityId;
layout(set=1, binding=3) uniform sampler    u_NearestSampler;
layout(set=1, binding=7) uniform sampler   u_LinearClampSampler;

layout(set=1, binding=4) uniform FxaaParamsUBO {
    float subpix;
    float edge_threshold;
    float edge_threshold_min;
    int mode;
    int debug_view;
    int _pad0;
    int _pad1;
    int _pad2;
} u_FxaaParams;

layout(set = 1, binding = 5) uniform texture2D       u_VectorTexture;
layout(set = 1, binding = 6) uniform itexture2D      u_VectorEntityTexture;


layout(location=0) in vec2 v_uv;

layout(location=0) out vec4 o_color;
layout(location=1) out int  o_entity_id;

void main() {

    vec2 rcp_frame = 1.0 / vec2(textureSize(sampler2D(u_LDRColor, u_LinearSampler), 0));
    vec3 color;
    if (u_FxaaParams.mode == 1) {
        color = fxaa(u_LDRColor, u_LinearClampSampler, v_uv, rcp_frame,
                u_FxaaParams.subpix, u_FxaaParams.edge_threshold, u_FxaaParams.edge_threshold_min, u_FxaaParams.debug_view);
    } else {
        color = texture(sampler2D(u_LDRColor, u_LinearSampler), v_uv).rgb;
    }

    // Picking
    int geometry_id = texelFetch(isampler2D(u_EntityId, u_NearestSampler), ivec2(gl_FragCoord.xy), 0).r;
    int vector_icon_id = texture(isampler2D(u_VectorEntityTexture, u_NearestSampler), v_uv).r;
    int picked_id = (vector_icon_id >= 0) ? vector_icon_id : geometry_id;

    // Overlay icons
    vec4 vector_icon = texture(sampler2D(u_VectorTexture, u_LinearSampler), v_uv);
    color = mix(color, vector_icon.rgb, vector_icon.a);

    o_entity_id = picked_id;
    o_color = vec4(color, 1.0);
}
