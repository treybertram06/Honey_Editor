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

layout(set=1, binding=0) uniform texture2D u_gNormal; // Oct-encoded world normals
layout(set=1, binding=1) uniform sampler   u_LinearSampler; // Oct-encoded world normals
layout(set=1, binding=2) uniform texture2D u_gDepth;
layout(set=1, binding=3) uniform sampler   u_NearestSampler;
layout(set=1, binding=4) uniform texture2D u_NoiseTexture;
// reuse depth sampler for noise - both are nearest

layout(set=1, binding=5) uniform SSAOKernelUBO {
    vec4 u_Samples[32]; // Hemisphere samples in view space
    vec2 u_NoiseScale; // screen_size / noise_size
    float u_Radius;
    float u_Bias;
} u_SSAOKernel;

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

// ---- SSAO pipeline debug visualization ------------------------------------
// Replace the occlusion output with an intermediate value so we can watch the
// data move through the pass. Inspect via the DeferredLighting SSAO_DEBUG view
// (which now shows the SSAO texture's full RGB). Change the number and the
// shader cache recompiles — no C++ rebuild needed.
//   0 = real AO (grayscale, 1=unoccluded)
//   1 = view-space normal                 (rgb = n*0.5+0.5; flat regions = solid color)
//   2 = linearized view depth             (grayscale, scaled by SSAO_DBG_DEPTH_SCALE)
//   3 = reconstructed view position       (rgb = fract(frag_pos); should vary smoothly)
//   4 = hemisphere sample coverage        (grayscale = fraction of samples that hit geometry)
//   5 = raw occlusion, no range_check     (grayscale, 1=fully occluded)
//   6 = noise rotation vector             (rgb)
//   7 = mean projected sample UV delta    (rg = avg |sample_uv - v_uv|, scaled x50; black = samples not moving)
//   8 = kernel UBO sanity                 (r=u_Radius, g=u_Bias, b=length(u_Samples[16])).
//       Healthy ≈ (0.5, 0.075, ~0.5) → dull magenta-ish. ALL BLACK = kernel buffer is zero.
#define SSAO_DEBUG_MODE 0
#define SSAO_DBG_DEPTH_SCALE 0.02

// Quick experiment knobs (no C++ rebuild): scale the kernel radius / depth bias set
// in C++ (0.5 / 0.025 view-units). Leave at 1.0 for normal rendering. Small props
// (FlightHelmet etc.) only show detail AO with a small radius+bias; big architecture
// reads better with larger ones — drop RADIUS_SCALE to ~0.3 to check fine detail.
#define SSAO_RADIUS_SCALE 1.0
#define SSAO_BIAS_SCALE 1.0

void main() {
    // 1. Reconstruct view-space position from depth
    float depth = texture(sampler2D(u_gDepth, u_NearestSampler), v_uv).r;
    vec4 ndc    = vec4(v_uv * 2.0 - 1.0, depth, 1.0);
    vec4 view_h = u_Camera.u_InvProjection * ndc;
    vec3 frag_pos = view_h.xyz / view_h.w;// view-space position
    // 2. Get view-space normal. Oct-encoded normals must be point-sampled: the SSAO
    // target and the gBuffer differ in size, so linear filtering would interpolate
    // between unrelated oct coordinates at edges and decode to garbage normals.
    vec2 oct_n = texture(sampler2D(u_gNormal, u_NearestSampler), v_uv).rg;
    vec3 world_n = oct_decode(oct_n);
    vec3 normal = normalize(mat3(u_Camera.u_View) * world_n);// to view space

    // 3. Build a TBN matrix from the noise rotation
    vec2 noise_uv = v_uv * u_SSAOKernel.u_NoiseScale;
    vec3 random_vec = normalize(vec3(texture(sampler2D(u_NoiseTexture, u_NearestSampler), noise_uv).rg * 2.0 - 1.0, 0.0));
    vec3 tangent   = normalize(random_vec - normal * dot(random_vec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normal);

    // 4. Sample the hemisphere
    float occlusion = 0.0;
    float raw_occlusion = 0.0;   // debug: occlusion without range_check weighting
    float on_geometry   = 0.0;   // debug: samples that landed on rasterized geometry
    vec2  uv_delta_sum  = vec2(0.0); // debug: how far projected samples move on screen
    float proj_a = u_Camera.u_Projection[2][2];
    float proj_b = -u_Camera.u_Projection[3][2];
    float radius = u_SSAOKernel.u_Radius * SSAO_RADIUS_SCALE;
    float bias   = u_SSAOKernel.u_Bias * SSAO_BIAS_SCALE;
    for (int i = 0; i < 32; ++i) {
        vec3 sample_pos = TBN * u_SSAOKernel.u_Samples[i].xyz;
        sample_pos = frag_pos + sample_pos * radius;

        // Project sample to get its UV and compare depth
        vec4 offset = u_Camera.u_Projection * vec4(sample_pos, 1.0);
        offset.xyz /= offset.w;
        vec2 sample_uv = offset.xy * 0.5 + 0.5;

        float raw_depth  = texture(sampler2D(u_gDepth, u_NearestSampler), sample_uv).r;
        float sample_depth = proj_b / (raw_depth + proj_a);

        // Range check: full weight while the occluder is within the kernel radius,
        // fading to zero by 2x radius. The old smoothstep(0,1, radius/|dz|) form still
        // gave 50% weight at 2x radius and ~16% at 4x, so foreground objects cast soft
        // "ghost" silhouettes onto walls metres behind them.
        float range_check = 1.0 - smoothstep(radius, radius * 2.0, abs(frag_pos.z - sample_depth));
        float occ = (sample_depth >= sample_pos.z + bias) ? 1.0 : 0.0;
        occlusion     += occ * range_check;
        raw_occlusion += occ;
        on_geometry   += (raw_depth < 1.0) ? 1.0 : 0.0;
        uv_delta_sum  += abs(sample_uv - v_uv);
    }

#if SSAO_DEBUG_MODE == 1
    o_color = vec4(normal * 0.5 + 0.5, 1.0);
#elif SSAO_DEBUG_MODE == 2
    o_color = vec4(vec3(-frag_pos.z * SSAO_DBG_DEPTH_SCALE), 1.0);
#elif SSAO_DEBUG_MODE == 3
    o_color = vec4(fract(frag_pos), 1.0);
#elif SSAO_DEBUG_MODE == 4
    o_color = vec4(vec3(on_geometry / 32.0), 1.0);
#elif SSAO_DEBUG_MODE == 5
    o_color = vec4(vec3(raw_occlusion / 32.0), 1.0);
#elif SSAO_DEBUG_MODE == 6
    o_color = vec4(random_vec * 0.5 + 0.5, 1.0);
#elif SSAO_DEBUG_MODE == 7
    o_color = vec4((uv_delta_sum / 32.0) * 50.0, 0.0, 1.0);
#elif SSAO_DEBUG_MODE == 8
    o_color = vec4(u_SSAOKernel.u_Radius,
                   u_SSAOKernel.u_Bias,
                   length(u_SSAOKernel.u_Samples[16].xyz),
                   1.0);
#else
    o_color = vec4(vec3(1.0 - (occlusion / 32.0)), 1.0);
#endif
}


