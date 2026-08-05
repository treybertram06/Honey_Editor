#type vertex
#version 450

layout(location = 0) out vec2 v_uv;

// Placeholder geometry only. Step 4 replaces this with real per-icon quads
// driven by an instance SSBO (world position, size, entity id, camera
// right/up basis expansion...). For now: one small fixed triangle near
// screen center at a fixed NDC depth, just enough to prove the pass
// renders, depth-tests against gBuffer, and composites correctly.
void main() {
    vec2 positions[3] = vec2[3](
            vec2( 0.0,  0.15),
            vec2(-0.15, -0.15),
            vec2( 0.15, -0.15)
    );
    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.5, 1.0); // fixed mid-range depth (0 = near, 1 = far)
    // NDC [-1,1] -> UV [0,1]. Vulkan NDC Y points down, texture V also increases down.
    v_uv = pos * 0.5 + 0.5;
}

#type fragment
#version 450

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 o_color;
layout(location = 1) out int  o_entity_id;

layout(set = 1, binding = 0) uniform texture2D u_gDepth;
layout(set = 1, binding = 1) uniform sampler   u_NearestSampler;

void main() {
    // gBuffer's depth target is full swapchain resolution, same as this
    // pass's own targets, so this fragment's screen UV can be recovered
    // straight from its window-space coordinate without a resolution uniform.
    vec2 depth_size = vec2(textureSize(sampler2D(u_gDepth, u_NearestSampler), 0));
    vec2 screen_uv  = gl_FragCoord.xy / depth_size;

    float scene_depth = texture(sampler2D(u_gDepth, u_NearestSampler), screen_uv).r;

    // Standard (non-reversed) depth: smaller = nearer. If real scene geometry
    // at this pixel is closer to the camera than this icon fragment, the
    // icon is occluded. This is the manual stand-in for hardware depthTest
    // now that this pass has no bound depth attachment of its own.
    if (gl_FragCoord.z > scene_depth)
        discard;

    o_color     = vec4(1.0, 0.75, 0.1, 1.0); // flat placeholder color
    o_entity_id = 1234; // placeholder sentinel — Step 4 wires the real per-icon entity id
}