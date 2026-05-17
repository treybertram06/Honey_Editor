#type vertex
#version 450

layout(location = 0) in vec3 a_position;
layout(location = 1) in uint a_normal_packed;
layout(location = 2) in uint a_tangent_packed;
layout(location = 3) in uint a_uv0_packed;

// Instance model matrix as 4 vec4s
layout(location = 5) in vec4 a_iModel0;
layout(location = 6) in vec4 a_iModel1;
layout(location = 7) in vec4 a_iModel2;
layout(location = 8) in vec4 a_iModel3;

layout(location = 9) in int a_iEntityID;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 u_ViewProjection;
    vec3 u_Position;
    float _pad0;
} u_Camera;

layout(location = 0) out vec2 v_uv0;
layout(location = 1) out vec2 v_uv1;
layout(location = 2) out vec3 v_normalWS;
layout(location = 3) out vec3 v_positionWS;
layout(location = 4) out vec4 v_tangentWS;
layout(location = 5) flat out int v_entityID_out;

#include "vertex_decode.glsl"

void main() {
    mat4 model = mat4(a_iModel0, a_iModel1, a_iModel2, a_iModel3);
    vec4 worldPos = model * vec4(a_position, 1.0);

    mat3 normalMat = transpose(inverse(mat3(model)));

    vec3 a_normal  = unpack_normal(a_normal_packed);
    vec4 a_tangent = unpack_tangent(a_tangent_packed);
    vec2 a_uv0     = unpackHalf2x16(a_uv0_packed);

    v_uv0         = a_uv0;
    v_uv1         = vec2(0.0);
    v_positionWS  = worldPos.xyz;
    v_normalWS    = normalize(normalMat * a_normal);
    v_tangentWS   = vec4(normalize(normalMat * a_tangent.xyz), a_tangent.w);
    v_entityID_out = a_iEntityID;

    gl_Position  = u_Camera.u_ViewProjection * worldPos;
}

#type fragment
#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2 v_uv0;
layout(location = 1) in vec2 v_uv1;
layout(location = 2) in vec3 v_normalWS;
layout(location = 3) in vec3 v_positionWS;
layout(location = 4) in vec4 v_tangentWS;
layout(location = 5) flat in int v_entityID;

// gAlbedo    (RGBA8):   RGB = linear albedo, A = alpha
// gNormal    (RGBA16F): RG = oct-encoded world-space normal, BA = emissive RGB (rg packed in B, b in A)
// gPBRParams (RGBA8):   R = metallic, G = roughness, B = AO, A = unused
// depth written automatically by the depth test
layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out vec4 o_pbr;

layout(set = 0, binding = 3) uniform sampler u_Sampler;
layout(set = 0, binding = 4) uniform texture2D u_Textures[];

struct GPUMaterial {
    vec4  base_color;
    vec4  emissive_factor;

    float metallic;
    float roughness;
    float normal_scale;
    float occlusion_strength;

    float alpha_cutoff;
    int   alpha_mode;
    int   double_sided;
    int   unlit;

    int   base_color_tex_id;
    int   metallic_roughness_tex_id;
    int   normal_tex_id;
    int   occlusion_tex_id;
    int   emissive_tex_id;

    int   base_color_uv_set;
    int   metallic_roughness_uv_set;
    int   normal_uv_set;
    int   occlusion_uv_set;
    int   emissive_uv_set;

    vec4  base_color_uv_scale_offset;
    vec4  metallic_roughness_uv_scale_offset;
    vec4  normal_uv_scale_offset;
    vec4  occlusion_uv_scale_offset;
    vec4  emissive_uv_scale_offset;

    float base_color_uv_rotation;
    float metallic_roughness_uv_rotation;
    float normal_uv_rotation;
    float occlusion_uv_rotation;
    float emissive_uv_rotation;

    float _pad0;
    float _pad1;
    float _pad2;
};
layout(set = 0, binding = 2) readonly buffer MaterialBuffer {
    GPUMaterial materials[];
} u_Materials;

layout(push_constant) uniform MaterialPC {
    int u_MaterialIndex;
    int _pad0, _pad1, _pad2;
} u_Material;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

vec2 select_uv(int set_index) {
    return set_index == 1 ? v_uv1 : v_uv0;
}

vec2 apply_uv_transform(vec2 uv, vec4 scale_offset, float rotation) {
    vec2 p = uv * scale_offset.xy;
    float c = cos(rotation);
    float s = sin(rotation);
    p = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
    return p + scale_offset.zw;
}

vec4 sample_texture(int tex_idx, vec2 uv) {
    return texture(sampler2D(u_Textures[nonuniformEXT(max(tex_idx, 0))], u_Sampler), uv);
}

// Sign-preserving octahedral encoding (Cigolle et al.).
// Encodes a unit vector into two float16-friendly values in [-1, 1].
// Decoding in the lighting pass: see oct_decode().
vec2 oct_encode(vec3 n) {
    vec2 p = n.xy / (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z <= 0.0)
        p = (1.0 - abs(p.yx)) * vec2(p.x >= 0.0 ? 1.0 : -1.0,
                                     p.y >= 0.0 ? 1.0 : -1.0);
    return p;
}

// Packs two float values in [0, 1] into a single float via 16-bit fixed-point.
// Used to store emissive.rg in gNormal.b and emissive.b (+ padding) in gNormal.a.
float pack_rg(float r, float g) {
    return floor(r * 255.0) / 256.0 + g / 256.0;
}

// ---------------------------------------------------------------------------

void main() {
    GPUMaterial mat = u_Materials.materials[u_Material.u_MaterialIndex];

    // --- Albedo ---
    vec2 base_uv = apply_uv_transform(select_uv(mat.base_color_uv_set),
                                      mat.base_color_uv_scale_offset,
                                      mat.base_color_uv_rotation);
    vec4 base_tex = sample_texture(mat.base_color_tex_id, base_uv);

    vec3 base_linear = (mat.base_color_tex_id >= 0) ? pow(base_tex.rgb, vec3(2.2)) : vec3(1.0);
    vec3 albedo = base_linear * mat.base_color.rgb;
    float alpha = base_tex.a * mat.base_color.a;

    if (mat.alpha_mode == 1 && alpha < mat.alpha_cutoff)
        discard;

    // --- Metallic / Roughness ---
    float metallic  = clamp(mat.metallic,  0.0, 1.0);
    float roughness = clamp(mat.roughness, 0.04, 1.0);

    if (mat.metallic_roughness_tex_id >= 0) {
        vec2 mr_uv = apply_uv_transform(select_uv(mat.metallic_roughness_uv_set),
                                        mat.metallic_roughness_uv_scale_offset,
                                        mat.metallic_roughness_uv_rotation);
        vec4 mr = sample_texture(mat.metallic_roughness_tex_id, mr_uv);
        roughness *= mr.g;
        metallic  *= mr.b;
    }

    // --- Normal (world-space, TBN applied) ---
    vec3 N = normalize(v_normalWS);
    if (mat.normal_tex_id >= 0) {
        vec2 n_uv = apply_uv_transform(select_uv(mat.normal_uv_set),
                                       mat.normal_uv_scale_offset,
                                       mat.normal_uv_rotation);
        vec3 nrm_ts = sample_texture(mat.normal_tex_id, n_uv).xyz * 2.0 - 1.0;
        nrm_ts.xy *= mat.normal_scale;

        vec3 T = normalize(v_tangentWS.xyz);
        vec3 B = normalize(cross(N, T)) * v_tangentWS.w;
        mat3 TBN = mat3(T, B, N);
        N = normalize(TBN * nrm_ts);
    }

    // --- AO ---
    float ao = 1.0;
    if (mat.occlusion_tex_id >= 0) {
        vec2 ao_uv = apply_uv_transform(select_uv(mat.occlusion_uv_set),
                                        mat.occlusion_uv_scale_offset,
                                        mat.occlusion_uv_rotation);
        ao = mix(1.0, sample_texture(mat.occlusion_tex_id, ao_uv).r,
                 clamp(mat.occlusion_strength, 0.0, 1.0));
    }

    // --- Emissive ---
    vec3 emissive = mat.emissive_factor.rgb;
    if (mat.emissive_tex_id >= 0) {
        vec2 e_uv = apply_uv_transform(select_uv(mat.emissive_uv_set),
                                       mat.emissive_uv_scale_offset,
                                       mat.emissive_uv_rotation);
        emissive *= pow(sample_texture(mat.emissive_tex_id, e_uv).rgb, vec3(2.2));
    }

    // --- Write G-buffer ---

    // gAlbedo: linear albedo RGB, alpha for alpha-masked geometry
    o_albedo = vec4(albedo, alpha);

    // gNormal: oct-encoded normal in RG ([-1,1] maps well to float16)
    //          emissive packed into BA: B = pack(emissive.r, emissive.g),
    //                                   A = pack(emissive.b, 0)
    // Emissive is clamped to [0,1] here; multiply by a scale in the lighting
    // pass if you need HDR emissive beyond 1.0.
    vec2 enc_n = oct_encode(N);
    o_normal = vec4(enc_n,
                    pack_rg(clamp(emissive.r, 0.0, 1.0), clamp(emissive.g, 0.0, 1.0)),
                    pack_rg(clamp(emissive.b, 0.0, 1.0), 0.0));

    // gPBRParams: metallic, roughness, AO
    o_pbr = vec4(metallic, roughness, ao, 0.0);
}