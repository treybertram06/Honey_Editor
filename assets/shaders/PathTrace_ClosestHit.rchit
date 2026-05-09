#version 460
#extension GL_EXT_ray_tracing           : require
#extension GL_EXT_buffer_reference2     : require
#extension GL_EXT_scalar_block_layout   : require
#extension GL_ARB_gpu_shader_int64      : require

// Constants
#define PI 3.1415926535987

// ─── Payload ──────────────────────────────────────────────────────────────────
struct HitPayload {
    vec3  radiance;
    float brdf_pdf;
    vec3  throughput;
    float _pad1;
    vec3  next_origin;
    float _pad2;
    vec3  next_direction;
    uint  seed;
    vec3  hit_normal;   // world-space shading normal (written for SVGF G-buffer)
    float hit_depth;    // ray travel distance (gl_HitTEXT)
};

layout(location = 0) rayPayloadInEXT HitPayload payload;
layout(location = 1) rayPayloadEXT bool shadow_payload;
hitAttributeEXT vec2 barycentrics;

// ─── Bindings ─────────────────────────────────────────────────────────────────
layout(set = 0, binding = 0) uniform accelerationStructureEXT tlas;

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
    vec4  base_color_factor;
    vec4  emissive_factor;
    int   base_color_tex;
    int   metal_rough_tex;
    int   normal_tex;
    int   emissive_tex;
    float metalness;
    float roughness;
    float normal_scale;
    float _pad;
};
layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer {
    MaterialInfo entries[];
} u_materials;

layout(set = 0, binding = 4) uniform sampler2D u_textures[512];

struct DirectionalLight { vec3 direction; float intensity; vec3 color; int point_light_count; };
struct PointLight        { vec3 position;  float intensity; vec3 color; float range; };
layout(set = 0, binding = 5) uniform LightsUBO {
    DirectionalLight u_directional;
    PointLight       u_point_lights[32];
} u_lights;

// ─── RNG ──────────────────────────────────────────────────────────────────────
uint rng_next(inout uint seed) {
    seed = seed * 747796405u + 2891336453u;
    uint w = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    return (w >> 22u) ^ w;
}
float rand_f(inout uint seed) { return float(rng_next(seed)) / 4294967295.0; }

vec2 rand2(inout uint seed) { return vec2(rand_f(seed), rand_f(seed)); }

// ─── Sampling ─────────────────────────────────────────────────────────────────
// Build an orthonormal basis around n.
void build_onb(vec3 n, out vec3 T, out vec3 B) {
    vec3 up = abs(n.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
    T = normalize(cross(up, n));
    B = cross(n, T);
}

// Cosine-weighted hemisphere sample. PDF = NdotL / pi.
// Throughput weight for Lambertian BRDF (albedo/pi): weight = albedo (cosine/pi cancel).
vec3 cosine_hemisphere(vec3 n, inout uint seed) {
    vec2 r  = rand2(seed);
    float phi = 6.28318530718 * r.x;
    float sq  = sqrt(r.y);
    vec3 T, B;
    build_onb(n, T, B);
    return normalize(T * (cos(phi)*sq) + B * (sin(phi)*sq) + n * sqrt(max(0.0, 1.0-r.y)));
}

// GGX NDF half-vector sample. Returns H in world space around n.
vec3 sample_ggx_H(float roughness, vec3 n, inout uint seed) {
    float a   = roughness * roughness;
    vec2  r   = rand2(seed);
    float phi = 6.28318530718 * r.x;
    // GGX importance sample of NdotH distribution
    float cos2 = (1.0 - r.y) / max(1.0 + (a*a - 1.0)*r.y, 1e-7);
    float cosT = sqrt(cos2);
    float sinT = sqrt(max(0.0, 1.0 - cos2));
    vec3 T, B;
    build_onb(n, T, B);
    vec3 H_local = vec3(cos(phi)*sinT, sin(phi)*sinT, cosT);
    return normalize(T*H_local.x + B*H_local.y + n*H_local.z);
}

// Sample with probability 0.3 toward the full dome, 0.7 toward a cone around the sun.
// Returns the mixture PDF via out param — caller must divide by it, not hardcode PI.
vec3 env_nee_direction(vec3 N, vec3 sun_dir, inout uint seed, out float pdf) {
    const float CONE_ANGLE   = 0.3;
    const float COS_CONE     = cos(CONE_ANGLE);
    const float CONE_PDF_VAL = 1.0 / (2.0 * PI * (1.0 - COS_CONE)); // = ~3.56

    if (rand_f(seed) < 0.3) {
        vec3  dir     = cosine_hemisphere(N, seed);
        float cos_pdf = max(dot(N, dir), 0.0) / PI;
        float in_cone = dot(dir, sun_dir) > COS_CONE ? CONE_PDF_VAL : 0.0;
        pdf = 0.3 * cos_pdf + 0.7 * in_cone;
        pdf = max(pdf, 1e-7);
        return dir;
    } else {
        vec2  r         = rand2(seed);
        float cos_theta = mix(COS_CONE, 1.0, r.x);
        float sin_theta = sqrt(max(0.0, 1.0 - cos_theta*cos_theta));
        float phi       = 6.28318 * r.y;
        vec3 T, B;
        build_onb(sun_dir, T, B);
        vec3 cone_dir = T*(cos(phi)*sin_theta) + B*(sin(phi)*sin_theta) + sun_dir*cos_theta;
        if (dot(cone_dir, N) > 0.0) {
            cone_dir       = normalize(cone_dir);
            float cos_pdf  = max(dot(N, cone_dir), 0.0) / PI;
            pdf = 0.3 * cos_pdf + 0.7 * CONE_PDF_VAL;
            pdf = max(pdf, 1e-7);
            return cone_dir;
        }
        // Cone sample went below surface — fall back to cosine hemisphere
        vec3  dir     = cosine_hemisphere(N, seed);
        float cos_pdf = max(dot(N, dir), 0.0) / PI;
        pdf = max(cos_pdf, 1e-7);
        return dir;
    }
}

// ─── GGX BRDF helpers ─────────────────────────────────────────────────────────
float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float d  = NdotH*NdotH*(a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G1_Smith(float NdotX, float a) {
    float a2 = a * a;
    return 2.0 * NdotX / (NdotX + sqrt(a2 + (1.0 - a2)*NdotX*NdotX));
}

float G2_Smith(float NdotV, float NdotL, float a) {
    return G1_Smith(NdotV, a) * G1_Smith(NdotL, a);
}

vec3 F_Schlick(float VdotH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

// ─── Sky dome radiance (no sun disc — directional light NEE covers that) ──────
// Used for environment NEE: explicitly samples the sky at each hit point so
// shadowed surfaces that can see the sky through openings converge in ~1 frame
// rather than waiting for random bounce rays to escape by chance.
// Intentionally omits the sun disc to avoid double-counting with directional NEE.
// Phase 4 will weight this against the bounce PDF using MIS.
vec3 sky_dome(vec3 ray_dir) {
    float sky_scale = u_lights.u_directional.intensity * 0.3;
    vec3  sky_tint  = mix(vec3(1.0), u_lights.u_directional.color, 0.5);
    float up        = max(ray_dir.y, 0.0);
    vec3  horizon   = vec3(0.80, 0.85, 0.90) * sky_tint * sky_scale;
    vec3  zenith    = vec3(0.20, 0.45, 0.85) * sky_tint * sky_scale;
    if (ray_dir.y < 0.0)
        return mix(vec3(0.12, 0.10, 0.09) * sky_scale, horizon, exp(ray_dir.y * 8.0));
    return mix(horizon, zenith, pow(up, 0.6));
}

// ─── Main ─────────────────────────────────────────────────────────────────────
void main() {
    // ── Geometry lookup ───────────────────────────────────────────────────────
    GeometryInfo geo = u_geometry.entries[gl_InstanceCustomIndexEXT];
    VertexBuffer vb  = VertexBuffer(geo.vertex_buf_addr);
    IndexBuffer  ib  = IndexBuffer(geo.index_buf_addr);

    uint base = uint(gl_PrimitiveID) * 3;
    uint i0 = ib.i[base];
    uint i1 = ib.i[base + 1];
    uint i2 = ib.i[base + 2];
    float w0 = 1.0 - barycentrics.x - barycentrics.y;
    float w1 = barycentrics.x;
    float w2 = barycentrics.y;

    // ── Interpolate vertex attributes ─────────────────────────────────────────
    // Normals (offset 3)
    vec3 n0 = vec3(vb.v[i0*VERTEX_STRIDE+3], vb.v[i0*VERTEX_STRIDE+4], vb.v[i0*VERTEX_STRIDE+5]);
    vec3 n1 = vec3(vb.v[i1*VERTEX_STRIDE+3], vb.v[i1*VERTEX_STRIDE+4], vb.v[i1*VERTEX_STRIDE+5]);
    vec3 n2 = vec3(vb.v[i2*VERTEX_STRIDE+3], vb.v[i2*VERTEX_STRIDE+4], vb.v[i2*VERTEX_STRIDE+5]);
    vec3 obj_normal = normalize(n0*w0 + n1*w1 + n2*w2);
    vec3 Ng = normalize(mat3(gl_ObjectToWorldEXT) * obj_normal); // geometric/shading normal

    // Tangents (offset 6, vec4 — .w = bitangent sign)
    vec4 t0 = vec4(vb.v[i0*VERTEX_STRIDE+6], vb.v[i0*VERTEX_STRIDE+7],
                   vb.v[i0*VERTEX_STRIDE+8], vb.v[i0*VERTEX_STRIDE+9]);
    vec4 t1 = vec4(vb.v[i1*VERTEX_STRIDE+6], vb.v[i1*VERTEX_STRIDE+7],
                   vb.v[i1*VERTEX_STRIDE+8], vb.v[i1*VERTEX_STRIDE+9]);
    vec4 t2 = vec4(vb.v[i2*VERTEX_STRIDE+6], vb.v[i2*VERTEX_STRIDE+7],
                   vb.v[i2*VERTEX_STRIDE+8], vb.v[i2*VERTEX_STRIDE+9]);
    vec4 obj_tangent4 = t0*w0 + t1*w1 + t2*w2;
    float bitan_sign  = obj_tangent4.w >= 0.0 ? 1.0 : -1.0;
    vec3 T_world = normalize(mat3(gl_ObjectToWorldEXT) * obj_tangent4.xyz);
    vec3 B_world = cross(Ng, T_world) * bitan_sign;
    mat3 TBN     = mat3(T_world, B_world, Ng); // tangent→world

    // UVs (offset 10)
    vec2 uv0 = vec2(vb.v[i0*VERTEX_STRIDE+10], vb.v[i0*VERTEX_STRIDE+11]);
    vec2 uv1 = vec2(vb.v[i1*VERTEX_STRIDE+10], vb.v[i1*VERTEX_STRIDE+11]);
    vec2 uv2 = vec2(vb.v[i2*VERTEX_STRIDE+10], vb.v[i2*VERTEX_STRIDE+11]);
    vec2 uv  = uv0*w0 + uv1*w1 + uv2*w2;

    // ── Material lookup ───────────────────────────────────────────────────────
    MaterialInfo mat = u_materials.entries[gl_InstanceCustomIndexEXT];

    vec4 albedo4 = mat.base_color_factor;
    if (mat.base_color_tex >= 0)
        albedo4 *= texture(u_textures[mat.base_color_tex], uv);
    vec3 albedo = albedo4.rgb;

    float metalness = mat.metalness;
    float roughness = mat.roughness;
    if (mat.metal_rough_tex >= 0) {
        vec4 mr = texture(u_textures[mat.metal_rough_tex], uv);
        // glTF packs: R=occlusion, G=roughness, B=metalness
        roughness *= mr.g;
        metalness *= mr.b;
    }
    roughness = clamp(roughness, 0.04, 1.0); // avoid degenerate specular at roughness=0

    // ── Normal map ────────────────────────────────────────────────────────────
    vec3 N = Ng;
    if (mat.normal_tex >= 0) {
        vec3 n_ts = texture(u_textures[mat.normal_tex], uv).rgb * 2.0 - 1.0;
        n_ts.xy  *= mat.normal_scale;
        N         = normalize(TBN * n_ts);
        // Flip N if it points away from the camera (back-face hit)
        if (dot(N, -gl_WorldRayDirectionEXT) < 0.0) N = -N;
    }

    // ── Derived BRDF quantities ───────────────────────────────────────────────
    vec3 V  = normalize(-gl_WorldRayDirectionEXT);
    vec3 F0 = mix(vec3(0.04), albedo, metalness);
    float a = roughness * roughness;

    float NdotV = max(dot(N, V), 1e-4);

    // ── Emissive term ─────────────────────────────────────────────────────────
    vec3 emissive = mat.emissive_factor.rgb;
    if (mat.emissive_tex >= 0)
        emissive *= texture(u_textures[mat.emissive_tex], uv).rgb;

    // ── Direct lighting ───────────────────────────────────────────────────────
    vec3 hit_pos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 Lo   = emissive;
    uint seed = payload.seed;

    // Directional light
    if (u_lights.u_directional.intensity > 0.0) {
        vec3 L     = normalize(-u_lights.u_directional.direction);
        vec3 H     = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0) {
            shadow_payload = true;
            traceRayEXT(tlas,
                gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
                0xFF, 0, 0, 1,
                hit_pos + N * 0.001, 0.001, L, 10000.0, 1);

            if (!shadow_payload) {
                float NdotH = max(dot(N, H), 0.0);
                float VdotH = max(dot(V, H), 0.0);

                // Specular contribution
                vec3  F  = F_Schlick(VdotH, F0);
                float D  = D_GGX(NdotH, a);
                float G2 = G2_Smith(NdotV, NdotL, a);
                vec3 f_s = (D * G2 * F) / max(4.0 * NdotV * NdotL, 1e-7);

                // Diffuse contribution (energy conserving: scaled by 1-F, killed for metals)
                vec3 f_d = (1.0 - F) * (1.0 - metalness) * albedo / PI;

                Lo += (f_d + f_s) * u_lights.u_directional.color
                    * u_lights.u_directional.intensity * NdotL;
            }
        }
    }

    // Point lights
    for (int li = 0; li < u_lights.u_directional.point_light_count; li++) {
        PointLight pl    = u_lights.u_point_lights[li];
        vec3 to_light    = pl.position - hit_pos;
        float dist       = length(to_light);
        if (dist > pl.range) continue;

        vec3  L     = to_light / dist;
        vec3  H     = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL == 0.0) continue;

        float atten   = 1.0 / (dist * dist);
        float window  = pow(max(1.0 - pow(dist / pl.range, 4.0), 0.0), 2.0);
        atten        *= window;

        shadow_payload = true;
        traceRayEXT(tlas,
            gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
            0xFF, 0, 0, 1,
            hit_pos + N * 0.001, 0.001, L, dist - 0.001, 1);

        if (!shadow_payload) {
            float NdotH = max(dot(N, H), 0.0);
            float VdotH = max(dot(V, H), 0.0);

            vec3  F  = F_Schlick(VdotH, F0);
            float D  = D_GGX(NdotH, a);
            float G2 = G2_Smith(NdotV, NdotL, a);
            vec3 f_s = (D * G2 * F) / max(4.0 * NdotV * NdotL, 1e-7);
            vec3 f_d = (1.0 - F) * (1.0 - metalness) * albedo / PI;

            Lo += (f_d + f_s) * pl.color * pl.intensity * NdotL * atten;
        }
    }

    // ── Environment NEE (sky dome) ────────────────────────────────────────────
    // Explicitly sample one sky direction per hit. If the shadow ray escapes,
    // add the sky dome's radiance weighted by the full BRDF.
    // PDF of cosine hemisphere = NdotL/π → estimator weight = π (cancels out below).
    // Note: bounce rays that also escape will add sky via the miss shader — Phase 4
    // MIS weighting will eliminate this double count; for now the bias is small
    // since most such paths are handled by one or the other, not both.
    {
        vec3 sun_dir   = normalize(-u_lights.u_directional.direction);

        float env_pdf;
        vec3 env_dir   = env_nee_direction(N, sun_dir, seed, env_pdf);
        float NdotL_e  = max(dot(N, env_dir), 0.0);
        shadow_payload = true;
        traceRayEXT(tlas,
            gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
            0xFF, 0, 0, 1,
            hit_pos + N * 0.001, 0.001, env_dir, 10000.0, 1);

        if (!shadow_payload && NdotL_e > 0.0) {
            vec3  H_e    = normalize(V + env_dir);
            float NdotH_e = max(dot(N, H_e), 0.0);
            float VdotH_e = max(dot(V, H_e), 0.0);

            vec3  F_e  = F_Schlick(VdotH_e, F0);
            float D_e  = D_GGX(NdotH_e, a);
            float G2_e = G2_Smith(NdotV, NdotL_e, a);
            vec3 f_s_e = (D_e * G2_e * F_e) / max(4.0 * NdotV * NdotL_e, 1e-7);
            vec3 f_d_e = (1.0 - F_e) * (1.0 - metalness) * albedo / PI;

            // MIS weight 0.5 (equal PDFs, diffuse approx) * Monte Carlo weight NdotL/pdf
            Lo += (f_d_e + f_s_e) * sky_dome(env_dir) * (NdotL_e / env_pdf) * 0.5;
        }
    }

    // ── Bounce ────────────────────────────────────────────────────────────────
    // Decide whether to take a specular or diffuse bounce.
    // Use the luminance of F0 as the specular selection probability.
    vec3  F0_approx   = F_Schlick(NdotV, F0);
    float p_specular  = max(F0_approx.r, max(F0_approx.g, F0_approx.b));
    p_specular        = clamp(p_specular, 0.0, 1.0);

    vec3 bounce_dir;
    vec3 bounce_throughput;

    if (rand_f(seed) < p_specular) {
        // ── Specular bounce ──
        vec3  H    = sample_ggx_H(roughness, N, seed);
        vec3  L    = reflect(-V, H);

        if (dot(L, N) <= 0.0) {
            payload.radiance       = Lo;
            payload.throughput     = vec3(0.0);
            payload.next_direction = vec3(0.0);
            payload.seed           = seed;
            payload.hit_normal     = N;
            payload.hit_depth      = gl_HitTEXT;
            return;
        }

        float NdotL = max(dot(N, L), 1e-4);
        float NdotH = max(dot(N, H), 1e-4);
        float VdotH = max(dot(V, H), 1e-4);

        vec3  F  = F_Schlick(VdotH, F0);
        float G2 = G2_Smith(NdotV, NdotL, a);
        float G1 = G1_Smith(NdotV, a);

        // GGX NDF-sampled weight: F * G2 / G1(NdotV)
        // (D and pi terms cancel with the sampling PDF)
        bounce_throughput = F * (G2 / max(G1, 1e-7)) / p_specular;
        bounce_dir        = L;
    } else {
        // ── Diffuse bounce ──
        bounce_dir        = cosine_hemisphere(N, seed);
        // (1 - F) * (1 - metalness) * albedo / pi * pi / costheta * costheta cancels to:
        vec3 F_at_normal  = F_Schlick(NdotV, F0);
        bounce_throughput = (1.0 - F_at_normal) * (1.0 - metalness) * albedo
                            / max(1.0 - p_specular, 1e-7);
    }

    payload.radiance       = Lo;
    payload.throughput     = bounce_throughput;
    payload.next_origin    = hit_pos + N * 0.001;
    payload.next_direction = bounce_dir;
    payload.seed           = seed;
    payload.hit_normal     = N;
    payload.hit_depth      = gl_HitTEXT;
}