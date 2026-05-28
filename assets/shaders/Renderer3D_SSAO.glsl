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

layout(set=1, binding=0) uniform sampler2D u_gNormal; // Oct-encoded world normals
layout(set=1, binding=1) uniform sampler2D u_gDepth;
layout(set=1, binding=2) uniform sampler2D u_NoiseTexture;

layout(set=1, binding=3) uniform SSAOKernelUBO {
    vec4 u_Samples[32]; // Hemisphere samples in view space
    vec2 u_NoiseScale; // screen_size / noise_size
    float u_Radius;
    float u_Bias;
} u_SSAO;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 u_ViewProjection;
    vec3 u_Position;
    float u_Exposure;
    mat4 u_InvViewProjection;
    mat4 u_View;
    mat4 u_Projection;
    mat4 u_InvProjection;
} u_Camera;

#include "CommonHelpers.glsl"

void main() {
    // 1. Reconstruct view-space position from depth
    float depth = texture(u_gDepth, v_uv).r;
    vec4 ndc    = vec4(v_uv * 2.0 - 1.0, depth, 1.0);
    vec4 view_h = u_Camera.u_InvProjection * ndc;
    vec3 frag_pos = view_h.xyz / view_h.w;// view-space position
    // 2. Get view-space normal
    vec2 oct_n = texture(u_gNormal, v_uv).rg;
    vec3 world_n = oct_decode(oct_n);
    vec3 normal = normalize(mat3(u_Camera.u_View) * world_n);// to view space

    // 3. Build a TBN matrix from the noise rotation
    vec2 noise_uv = v_uv * u_SSAO.u_NoiseScale;
    vec3 random_vec = normalize(vec3(texture(u_NoiseTexture, noise_uv).rg * 2.0 - 1.0, 0.0));
    vec3 tangent   = normalize(random_vec - normal * dot(random_vec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normal);

    // 4. Sample the hemisphere
    float occlusion = 0.0;
    float proj_a = u_Camera.u_Projection[2][2];
    float proj_b = -u_Camera.u_Projection[3][2];
    for (int i = 0; i < 32; ++i) {
        vec3 sample_pos = TBN * u_SSAO.u_Samples[i].xyz;
        sample_pos = frag_pos + sample_pos * u_SSAO.u_Radius;

        // Project sample to get its UV and compare depth
        vec4 offset = u_Camera.u_Projection * vec4(sample_pos, 1.0);
        offset.xyz /= offset.w;
        vec2 sample_uv = offset.xy * 0.5 + 0.5;

        float raw_depth  = texture(u_gDepth, sample_uv).r;
        float sample_depth = proj_b / (raw_depth + proj_a);

        // Range check: only count samples within a reasonable radius
        float range_check = smoothstep(0.0, 1.0, u_SSAO.u_Radius / abs(frag_pos.z - sample_depth));
        occlusion += (sample_depth >= sample_pos.z + u_SSAO.u_Bias ? 1.0 : 0.0) * range_check;
    }

    o_color = vec4(vec3(1.0 - (occlusion / 32.0)), 1.0);
}


