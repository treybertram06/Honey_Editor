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

#define MAX_POINT_LIGHTS 32
#define TILE_SIZE 16

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 o_color;
layout(location = 1) out int  o_entity_id;

// ---- set=0 global descriptors ----
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 u_ViewProjection;
    vec3 u_Position;
    float _pad0;
    mat4 u_InvViewProjection;
} u_Camera;

struct DirectionalLight {
    vec3  direction;
    float intensity;
    vec3  color;
    int   point_light_count;
};
struct PointLight {
    vec3  position;
    float intensity;
    vec3  color;
    float range;
};
layout(set = 0, binding = 1) uniform LightsUBO {
    DirectionalLight u_DirectionalLight;
    PointLight       u_PointLights[MAX_POINT_LIGHTS];
} u_Lights;

layout(set = 0, binding = 5, std430) readonly buffer TiledLightingBuffer {
    uint tile_count_x;
    uint tile_count_y;
    uint light_count;
    uint _pad;
    uint sorted_light_indices[MAX_POINT_LIGHTS];
    uint tile_light_masks[];
} u_TiledLighting;

// ---- set=1 G-buffer textures ----
layout(set = 1, binding = 0) uniform sampler2D u_gAlbedo;
layout(set = 1, binding = 1) uniform sampler2D u_gNormal;
layout(set = 1, binding = 2) uniform sampler2D u_gPBRParams;
layout(set = 1, binding = 3) uniform sampler2D u_gDepth;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

vec3 oct_decode(vec2 p) {
    vec3 n = vec3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0,
                                         n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

vec2 unpack_rg(float v) {
    float r = floor(v * 256.0) / 255.0;
    float g = fract(v * 256.0);
    return vec2(r, g);
}

float distribution_ggx(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d);
}

float geometry_schlick_ggx(float NdotV, float roughness) {
    float k = (roughness + 1.0);
    k = (k * k) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometry_smith(float NdotV, float NdotL, float roughness) {
    return geometry_schlick_ggx(NdotV, roughness) *
           geometry_schlick_ggx(NdotL, roughness);
}

vec3 fresnel_schlick(float cos_theta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

vec3 pbr_point_light(vec3 world_pos, vec3 N, vec3 V, vec3 F0, vec3 albedo, float metallic, float roughness, uint light_idx) {
    PointLight pl = u_Lights.u_PointLights[light_idx];
    vec3  to_light = pl.position - world_pos;
    float dist     = length(to_light);
    if (dist >= pl.range) return vec3(0.0);

    vec3  L     = to_light / dist;
    vec3  H     = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);

    float t           = dist / pl.range;
    float attenuation = clamp(1.0 - t * t * t * t, 0.0, 1.0);
    attenuation      *= attenuation / (dist * dist + 1.0);

    float D = distribution_ggx(NdotH, roughness);
    float G = geometry_smith(NdotV, NdotL, roughness);
    vec3  F = fresnel_schlick(max(dot(H, V), 0.0), F0);

    vec3 kD       = (1.0 - F) * (1.0 - metallic);
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    return (kD * albedo / 3.14159265 + specular) * pl.color * pl.intensity * NdotL * attenuation;
}

// ---------------------------------------------------------------------------

void main() {
    float depth = texture(u_gDepth, v_uv).r;

    // Reconstruct world position from depth
    vec4 ndc_pos   = vec4(v_uv * 2.0 - 1.0, depth, 1.0);
    vec4 world_h   = u_Camera.u_InvViewProjection * ndc_pos;
    vec3 world_pos = world_h.xyz / world_h.w;

    // Sample G-buffer
    vec4 albedo_samp = texture(u_gAlbedo,    v_uv);
    vec4 normal_samp = texture(u_gNormal,    v_uv);
    vec4 pbr_samp    = texture(u_gPBRParams, v_uv);

    // Unpack G-buffer
    vec3  albedo   = albedo_samp.rgb;
    vec2  oct_n    = normal_samp.rg;
    vec3  N        = oct_decode(oct_n);

    vec2  em_rg    = unpack_rg(normal_samp.b);
    float em_b     = unpack_rg(normal_samp.a).r;
    vec3  emissive = vec3(em_rg, em_b);

    float metallic  = pbr_samp.r;
    float roughness = max(pbr_samp.g, 0.04);
    float ao        = pbr_samp.b;

    // Background: no geometry written (depth == 1.0), skip lighting
    if (depth >= 1.0) {
        o_color     = vec4(albedo, 1.0);
        o_entity_id = -1;
        return;
    }

    vec3 V  = normalize(u_Camera.u_Position - world_pos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    // Directional light
    if (u_Lights.u_DirectionalLight.intensity > 0.0) {
        vec3  L    = normalize(-u_Lights.u_DirectionalLight.direction);
        vec3  H    = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.001);
        float NdotH = max(dot(N, H), 0.0);

        float D = distribution_ggx(NdotH, roughness);
        float G = geometry_smith(NdotV, NdotL, roughness);
        vec3  F = fresnel_schlick(max(dot(H, V), 0.0), F0);

        vec3 kD       = (1.0 - F) * (1.0 - metallic);
        vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

        Lo += (kD * albedo / 3.14159265 + specular) *
              u_Lights.u_DirectionalLight.color * u_Lights.u_DirectionalLight.intensity * NdotL;
    }

    // Tiled point lights
    ivec2 tile_coord = ivec2(gl_FragCoord.xy) / TILE_SIZE;
    uint  tile_index = uint(tile_coord.y) * u_TiledLighting.tile_count_x + uint(tile_coord.x);
    uint  mask       = u_TiledLighting.tile_light_masks[tile_index];
    while (mask != 0u) {
        uint bit = uint(findLSB(int(mask)));
        mask &= mask - 1u;
        Lo += pbr_point_light(world_pos, N, V, F0, albedo, metallic, roughness, bit);
    }

    // Ambient
    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color   = ambient + Lo + emissive;

    // Reinhard tonemap + gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    o_color     = vec4(color, 1.0);
    o_entity_id = -1;
}