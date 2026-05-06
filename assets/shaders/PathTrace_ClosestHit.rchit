#version 460
#extension GL_EXT_ray_tracing           : require
#extension GL_EXT_buffer_reference2     : require
#extension GL_EXT_scalar_block_layout   : require
#extension GL_ARB_gpu_shader_int64      : require

// Must match RayGen and Miss shaders exactly
struct HitPayload {
    vec3  radiance;
    float _pad0;
    vec3  throughput;
    float _pad1;
    vec3  next_origin;
    float _pad2;
    vec3  next_direction;
    uint  seed;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;
layout(location = 1) rayPayloadEXT bool shadow_payload;
hitAttributeEXT vec2 barycentrics;

// TLAS — needed so the closest hit shader can trace shadow rays
layout(set = 0, binding = 0) uniform accelerationStructureEXT tlas;

// VertexPBR layout (56 bytes = 14 floats):
//   [0..2]  position  vec3
//   [3..5]  normal    vec3
//   [6..9]  tangent   vec4
//   [10..11] uv0      vec2
//   [12..13] uv1      vec2
#define VERTEX_STRIDE 14

layout(buffer_reference, scalar) readonly buffer VertexBuffer { float v[]; };
layout(buffer_reference, scalar) readonly buffer IndexBuffer  { uint  i[]; };

struct GeometryInfo {
    uint64_t vertex_buf_addr;
    uint64_t index_buf_addr;
};
layout(set = 0, binding = 2, std430) readonly buffer GeometryLookup {
    GeometryInfo entries[];
} u_geometry;

struct MaterialInfo {
    vec4 base_color_factor;
    int  texture_index;
    float _pad[3];
};
layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer {
    MaterialInfo entries[];
} u_materials;

layout(set = 0, binding = 4) uniform sampler2D u_textures[512];

struct DirectionalLight {
    vec3  direction; // world-space, points DOWN from light; negate for toward-light vector L
    float intensity;
    vec3  color;
    int   point_light_count;
};
struct PointLight { vec3 position; float intensity; vec3 color; float range; };

layout(set = 0, binding = 5) uniform LightsUBO {
    DirectionalLight u_directional;
    PointLight       u_point_lights[32];
} u_lights;

// PCG hash RNG
uint rng_next(inout uint seed) {
    seed = seed * 747796405u + 2891336453u;
    uint w = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    return (w >> 22u) ^ w;
}
float rand_float(inout uint seed) {
    return float(rng_next(seed)) / 4294967295.0;
}

// Cosine-weighted hemisphere sample around `normal`.
// PDF = cos(theta)/pi; Lambertian BRDF = albedo/pi → throughput weight = albedo (no extra factor).
vec3 cosine_hemisphere(vec3 normal, inout uint seed) {
    float r1  = rand_float(seed);
    float r2  = rand_float(seed);
    float phi = 6.28318530718 * r1;
    float sq  = sqrt(r2);
    float x   = cos(phi) * sq;
    float y   = sin(phi) * sq;
    float z   = sqrt(max(0.0, 1.0 - r2));

    vec3 up        = abs(normal.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent   = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * x + bitangent * y + normal * z);
}

void main() {
    GeometryInfo geo = u_geometry.entries[gl_InstanceCustomIndexEXT];
    VertexBuffer vb  = VertexBuffer(geo.vertex_buf_addr);
    IndexBuffer  ib  = IndexBuffer(geo.index_buf_addr);

    uint base = uint(gl_PrimitiveID) * 3;
    uint i0 = ib.i[base + 0];
    uint i1 = ib.i[base + 1];
    uint i2 = ib.i[base + 2];

    float w0 = 1.0 - barycentrics.x - barycentrics.y;

    // Interpolate normals (offset 3 in 14-float vertex)
    vec3 n0 = vec3(vb.v[i0 * VERTEX_STRIDE + 3],
                   vb.v[i0 * VERTEX_STRIDE + 4],
                   vb.v[i0 * VERTEX_STRIDE + 5]);
    vec3 n1 = vec3(vb.v[i1 * VERTEX_STRIDE + 3],
                   vb.v[i1 * VERTEX_STRIDE + 4],
                   vb.v[i1 * VERTEX_STRIDE + 5]);
    vec3 n2 = vec3(vb.v[i2 * VERTEX_STRIDE + 3],
                   vb.v[i2 * VERTEX_STRIDE + 4],
                   vb.v[i2 * VERTEX_STRIDE + 5]);
    vec3 obj_normal   = normalize(n0 * w0 + n1 * barycentrics.x + n2 * barycentrics.y);
    vec3 world_normal = normalize(mat3(gl_ObjectToWorldEXT) * obj_normal);

    // Interpolate UVs (offset 10 in 14-float vertex)
    vec2 uv0 = vec2(vb.v[i0 * VERTEX_STRIDE + 10], vb.v[i0 * VERTEX_STRIDE + 11]);
    vec2 uv1 = vec2(vb.v[i1 * VERTEX_STRIDE + 10], vb.v[i1 * VERTEX_STRIDE + 11]);
    vec2 uv2 = vec2(vb.v[i2 * VERTEX_STRIDE + 10], vb.v[i2 * VERTEX_STRIDE + 11]);
    vec2 uv  = uv0 * w0 + uv1 * barycentrics.x + uv2 * barycentrics.y;

    MaterialInfo mat = u_materials.entries[gl_InstanceCustomIndexEXT];
    vec4 albedo = mat.base_color_factor;
    if (mat.texture_index >= 0)
        albedo *= texture(u_textures[mat.texture_index], uv);

    vec3 hit_pos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // --- Direct lighting ---
    vec3 Lo = vec3(0.0);

    // Directional light
    if (u_lights.u_directional.intensity > 0.0) {
        vec3 L = normalize(-u_lights.u_directional.direction);
        float NdotL = max(dot(world_normal, L), 0.0);
        if (NdotL > 0.0) {
            shadow_payload = true;
            traceRayEXT(tlas,
                gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
                0xFF, 0, 0,
                1, // miss shader index 1 → shadow miss
                hit_pos + world_normal * 0.001, 0.001, L, 10000.0,
                1); // payload location 1
            if (!shadow_payload)
                Lo += albedo.rgb * u_lights.u_directional.color * u_lights.u_directional.intensity * NdotL;
        }
    }

    // Point lights
    for (int i = 0; i < u_lights.u_directional.point_light_count; i++) {
        PointLight pl   = u_lights.u_point_lights[i];
        vec3 to_light   = pl.position - hit_pos;
        float dist      = length(to_light);
        if (dist > pl.range) continue;

        vec3 L = to_light / dist;
        float NdotL = max(dot(world_normal, L), 0.0);
        if (NdotL == 0.0) continue;

        // Inverse-square attenuation clamped at range
        float atten = clamp(1.0 - dist / pl.range, 0.0, 1.0);
        atten *= atten;

        shadow_payload = true;
        traceRayEXT(tlas,
            gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
            0xFF, 0, 0,
            1, // shadow miss
            hit_pos + world_normal * 0.001, 0.001, L, dist - 0.001,
            1);
        if (!shadow_payload)
            Lo += albedo.rgb * pl.color * pl.intensity * NdotL * atten;
    }

    // --- Bounce ---
    uint seed = payload.seed;
    vec3 bounce_dir = cosine_hemisphere(world_normal, seed);

    payload.radiance       = Lo;
    payload.throughput     = albedo.rgb; // cosine sampling cancels the cos/pdf factor for Lambertian
    payload.next_origin    = hit_pos + world_normal * 0.001;
    payload.next_direction = bounce_dir;
    payload.seed           = seed;
}