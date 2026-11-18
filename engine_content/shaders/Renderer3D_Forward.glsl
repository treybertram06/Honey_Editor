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
    mat4 u_InvViewProjection;
    mat4 u_View;
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

layout(location = 0) out vec4 o_color;
layout(location = 1) out int v_entityID_out;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 u_ViewProjection;
    vec3 u_CameraPos;
    float _pad0;
    mat4 u_InvViewProjection;
    mat4 u_View;
} u_Camera;

layout(set = 0, binding = 3) uniform sampler u_Sampler;
layout(set = 0, binding = 4) uniform texture2D u_Textures[];

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
    PointLight       u_PointLights[32];
} u_Lights;

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

struct ShadowLightMatrices {
    mat4  face_view_proj[6];
    vec3  position;
    float range;
};
layout(set = 0, binding = 6, std430) readonly buffer ShadowMatricesBuffer {
    uint                shadow_light_count;
    uint                shadow_light_point_indices[8];
    uint                _pad2[3];
    ShadowLightMatrices lights[];
} u_ShadowMatrices;

layout(set = 0, binding = 7, std430) readonly buffer DirShadowBuffer {
    mat4  cascade_vp[4];
    float cascade_splits[4];
    uint  cascade_count;
    uint  enabled;
    float shadow_distance;
    uint  _pad;
} u_DirShadow;

layout(set = 0, binding = 8) uniform samplerCubeArrayShadow u_ShadowCubeArray;
layout(set = 0, binding = 9) uniform sampler2DArrayShadow   u_ShadowDirMap;

layout(push_constant) uniform MaterialPC {
    int u_MaterialIndex;
    int _pad0, _pad1, _pad2;
} u_Material;

const float PI = 3.14159265359;

float ndf_ggx(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geometry_schlick(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float geometry_smith(float NdotV, float NdotL, float roughness) {
    return geometry_schlick(NdotV, roughness) * geometry_schlick(NdotL, roughness);
}

vec3 fresnel_schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, vec3 F0, vec3 radiance) {
    vec3  H     = normalize(V + L);
    float NdotV = max(dot(N, V), 0.001);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float NDF = ndf_ggx(NdotH, roughness);
    float G   = geometry_smith(NdotV, NdotL, roughness);
    vec3  F   = fresnel_schlick(HdotV, F0);

    vec3  num         = NDF * G * F;
    float denom       = 4.0 * NdotV * NdotL + 0.001;
    vec3  specular    = num / denom;

    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

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

// Converts world-space fragment-to-light distance to shadow map NDC depth.
float shadow_ndc_depth(float dist, float range) {
    const float near = 0.05;
    return (range * (dist - near)) / (dist * (range - near));
}

// 8-tap PCF lookup into the point light shadow cubemap array.
float sample_point_shadow(int shadow_slot, vec3 world_pos) {
    vec3  dir  = world_pos - u_ShadowMatrices.lights[shadow_slot].position;
    float dist = max(abs(dir.x), max(abs(dir.y), abs(dir.z)));
    float ref  = clamp(shadow_ndc_depth(dist, u_ShadowMatrices.lights[shadow_slot].range) - 0.0001, 0.0, 1.0);

    vec3 p1 = normalize(cross(dir, vec3(0.0, 1.0, 0.01)));
    vec3 p2 = normalize(cross(dir, p1));
    const float k_pcf_scale = 0.003;
    float r = dist * k_pcf_scale;

    const vec2 taps[8] = vec2[8](
        vec2( 1.000,  0.000), vec2( 0.707,  0.707),
        vec2( 0.000,  1.000), vec2(-0.707,  0.707),
        vec2(-1.000,  0.000), vec2(-0.707, -0.707),
        vec2( 0.000, -1.000), vec2( 0.707, -0.707)
    );
    float s = 0.0;
    for (int i = 0; i < 8; ++i) {
        vec3 offset = taps[i].x * p1 + taps[i].y * p2;
        s += texture(u_ShadowCubeArray, vec4(dir + offset * r, float(shadow_slot)), ref);
    }
    return s / 8.0;
}

// 3x3 PCF lookup into the directional shadow cascade array.
float sample_dir_shadow(vec3 world_pos) {
    if (u_DirShadow.enabled == 0u) return 1.0;

    float view_z = abs((u_Camera.u_View * vec4(world_pos, 1.0)).z);

    float fade_start = u_DirShadow.shadow_distance * 0.85;
    float dist_fade  = 1.0 - smoothstep(fade_start, u_DirShadow.shadow_distance, view_z);
    if (dist_fade <= 0.0) return 1.0;

    uint cascade = u_DirShadow.cascade_count - 1u;
    for (uint i = 0u; i < u_DirShadow.cascade_count; ++i) {
        if (view_z < u_DirShadow.cascade_splits[i]) { cascade = i; break; }
    }

    vec4 ls   = u_DirShadow.cascade_vp[cascade] * vec4(world_pos, 1.0);
    vec3 proj = ls.xyz / ls.w;
    vec2 uv   = proj.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;

    float ref   = clamp(proj.z - 0.0005, 0.0, 1.0);
    float s     = 0.0;
    vec2  texel = 1.0 / vec2(float(4096));
    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y)
        s += texture(u_ShadowDirMap, vec4(uv + vec2(x, y) * texel, float(cascade), ref));

    return mix(s / 9.0, 1.0, 1.0 - dist_fade);
}

void main() {
    GPUMaterial mat = u_Materials.materials[u_Material.u_MaterialIndex];

    vec2 base_uv = apply_uv_transform(select_uv(mat.base_color_uv_set),
                                      mat.base_color_uv_scale_offset,
                                      mat.base_color_uv_rotation);
    vec4 base_tex = sample_texture(mat.base_color_tex_id, base_uv);

    vec3 base_tex_linear = (mat.base_color_tex_id >= 0) ? pow(base_tex.rgb, vec3(2.2)) : vec3(1.0);
    vec3 albedo = base_tex_linear * mat.base_color.rgb;
    float alpha = base_tex.a * mat.base_color.a;

    if (mat.alpha_mode == 1 && alpha < mat.alpha_cutoff) {
        discard;
    }

    float metallic = clamp(mat.metallic, 0.0, 1.0);
    float roughness = clamp(mat.roughness, 0.04, 1.0);

    if (mat.metallic_roughness_tex_id >= 0) {
        vec2 mr_uv = apply_uv_transform(select_uv(mat.metallic_roughness_uv_set),
                                        mat.metallic_roughness_uv_scale_offset,
                                        mat.metallic_roughness_uv_rotation);
        vec4 mr = sample_texture(mat.metallic_roughness_tex_id, mr_uv);
        roughness *= mr.g;
        metallic *= mr.b;
    }

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

    vec3 emissive = mat.emissive_factor.rgb;
    if (mat.emissive_tex_id >= 0) {
        vec2 e_uv = apply_uv_transform(select_uv(mat.emissive_uv_set),
                                       mat.emissive_uv_scale_offset,
                                       mat.emissive_uv_rotation);
        vec3 e_tex = pow(sample_texture(mat.emissive_tex_id, e_uv).rgb, vec3(2.2));
        emissive *= e_tex;
    }

    float ao = 1.0;
    if (mat.occlusion_tex_id >= 0) {
        vec2 ao_uv = apply_uv_transform(select_uv(mat.occlusion_uv_set),
                                        mat.occlusion_uv_scale_offset,
                                        mat.occlusion_uv_rotation);
        float occ = sample_texture(mat.occlusion_tex_id, ao_uv).r;
        ao = mix(1.0, occ, clamp(mat.occlusion_strength, 0.0, 1.0));
    }

    vec3 V = normalize(u_Camera.u_CameraPos - v_positionWS);

    vec3 color;
    if (mat.unlit != 0) {
        color = albedo + emissive;
    } else {
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 Lo = vec3(0.0);

        if (u_Lights.u_DirectionalLight.intensity > 0.0) {
            vec3 L = normalize(-u_Lights.u_DirectionalLight.direction);
            vec3 radiance = u_Lights.u_DirectionalLight.color * u_Lights.u_DirectionalLight.intensity;
            float shadow = sample_dir_shadow(v_positionWS);
            Lo += brdf(N, V, L, albedo, metallic, roughness, F0, radiance) * shadow;
        }

        int count = u_Lights.u_DirectionalLight.point_light_count;
        for (int i = 0; i < count; ++i) {
            PointLight pl = u_Lights.u_PointLights[i];
            vec3 delta = pl.position - v_positionWS;
            float dist = length(delta);
            if (dist >= pl.range) continue;

            vec3 L = normalize(delta);
            float attenuation = 1.0 / (dist * dist);
            float window = pow(max(1.0 - pow(dist / pl.range, 4.0), 0.0), 2.0);
            vec3 radiance = pl.color * pl.intensity * attenuation * window;
            vec3 contrib = brdf(N, V, L, albedo, metallic, roughness, F0, radiance);

            // Apply shadow if this point light has a shadow slot.
            for (uint si = 0u; si < u_ShadowMatrices.shadow_light_count; ++si) {
                if (u_ShadowMatrices.shadow_light_point_indices[si] == uint(i)) {
                    contrib *= sample_point_shadow(int(si), v_positionWS);
                    break;
                }
            }

            Lo += contrib;
        }

        vec3 ambient = vec3(0.03) * albedo * ao;
        color = ambient + Lo + emissive;
    }

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    if (mat.alpha_mode == 0)
        alpha = 1.0;

    o_color = vec4(color, alpha);
    v_entityID_out = v_entityID;
}