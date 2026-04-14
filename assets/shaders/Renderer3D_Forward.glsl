#type vertex
#version 450

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
layout(location = 3) in vec2 a_uv0;
layout(location = 4) in vec2 a_uv1;

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

void main() {
    mat4 model = mat4(a_iModel0, a_iModel1, a_iModel2, a_iModel3);
    vec4 worldPos = model * vec4(a_position, 1.0);

    mat3 normalMat = transpose(inverse(mat3(model)));

    v_uv0         = a_uv0;
    v_uv1         = a_uv1;
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

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 u_ViewProjection;
    vec3 u_CameraPos;
} u_Camera;

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
            Lo += brdf(N, V, L, albedo, metallic, roughness, F0, radiance);
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
            Lo += brdf(N, V, L, albedo, metallic, roughness, F0, radiance);
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
