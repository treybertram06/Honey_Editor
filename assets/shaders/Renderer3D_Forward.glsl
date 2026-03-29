#type vertex
#version 450

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

// Instance model matrix as 4 vec4s
layout(location = 3) in vec4 a_iModel0;
layout(location = 4) in vec4 a_iModel1;
layout(location = 5) in vec4 a_iModel2;
layout(location = 6) in vec4 a_iModel3;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 u_ViewProjection;
    vec3 u_Position;
    float _pad0;
} u_Camera;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec3 v_normalWS;
layout(location = 2) out vec3 v_positionWS;

void main() {
    mat4 model = mat4(a_iModel0, a_iModel1, a_iModel2, a_iModel3);
    vec4 worldPos = model * vec4(a_position, 1.0);

    v_uv         = a_uv;
    v_positionWS = worldPos.xyz;
    v_normalWS   = normalize(transpose(inverse(mat3(model))) * a_normal);

    gl_Position  = u_Camera.u_ViewProjection * worldPos;
}

#type fragment
#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_normalWS;
layout(location = 2) in vec3 v_positionWS;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = 3) uniform sampler u_Sampler;
layout(set = 0, binding = 4) uniform texture2D u_Textures[32];

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
    float metallic;
    float roughness;
    int   base_color_tex_index;
    int   _pad;
};
layout(set = 0, binding = 2) readonly buffer MaterialBuffer {
    GPUMaterial materials[];
} u_Materials;

layout(push_constant) uniform MaterialPC {
    int u_MaterialIndex;
    int _pad0, _pad1, _pad2;
} u_Material;

const float PI = 3.14159265359;

// Normal Distribution Function — GGX/Trowbridge-Reitz
// How much of the microfacet surface is aligned with the halfway vector
float ndf_ggx(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Geometry function — Smith/Schlick-GGX
// Models self-shadowing of microfacets
float geometry_schlick(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float geometry_smith(float NdotV, float NdotL, float roughness) {
    return geometry_schlick(NdotV, roughness) * geometry_schlick(NdotL, roughness);
}

// Fresnel — Schlick approximation
// How much light reflects vs refracts at grazing angles
vec3 fresnel_schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Full Cook-Torrance specular BRDF + Lambertian diffuse for one light
vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, vec3 F0, vec3 radiance) {
    vec3  H     = normalize(V + L);
    float NdotV = max(dot(N, V), 0.001);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float NDF = ndf_ggx(NdotH, roughness);
    float G   = geometry_smith(NdotV, NdotL, roughness);
    vec3  F   = fresnel_schlick(HdotV, F0);

    // Cook-Torrance specular
    vec3  num         = NDF * G * F;
    float denom       = 4.0 * NdotV * NdotL + 0.001;
    vec3  specular    = num / denom;

    // Energy conservation: metals have no diffuse
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

void main() {
    GPUMaterial mat = u_Materials.materials[u_Material.u_MaterialIndex];

    int  idx    = clamp(mat.base_color_tex_index, 0, 31);
    vec4 base   = texture(sampler2D(u_Textures[nonuniformEXT(idx)], u_Sampler), v_uv)
                  * mat.base_color;

    float metallic  = mat.metallic;
    float roughness = mat.roughness;

    vec3 albedo = pow(base.rgb, vec3(2.2));
    vec3 N      = normalize(v_normalWS);
    vec3 V      = normalize(u_Camera.u_CameraPos - v_positionWS);

    // F0: base reflectivity. 0.04 is a good default for dielectrics.
    // Metals use their albedo color as F0.
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    // Directional light
    if (u_Lights.u_DirectionalLight.intensity > 0.0) {
        vec3 L        = normalize(-u_Lights.u_DirectionalLight.direction);
        vec3 radiance = u_Lights.u_DirectionalLight.color
                        * u_Lights.u_DirectionalLight.intensity;
        Lo += brdf(N, V, L, albedo, metallic, roughness, F0, radiance);
    }

    // Point lights
    int count = u_Lights.u_DirectionalLight.point_light_count;
    for (int i = 0; i < count; ++i) {
        PointLight pl   = u_Lights.u_PointLights[i];
        vec3  delta     = pl.position - v_positionWS;
        float dist      = length(delta);

        // Skip if outside range
        if (dist >= pl.range) continue;

        vec3  L           = normalize(delta);
        float attenuation = 1.0 / (dist * dist);   // physically correct inverse square

        // Windowing function: smoothly cuts off at range boundary
        float window      = pow(max(1.0 - pow(dist / pl.range, 4.0), 0.0), 2.0);
        vec3  radiance    = pl.color * pl.intensity * attenuation * window;

        Lo += brdf(N, V, L, albedo, metallic, roughness, F0, radiance);
    }

    // Ambient — placeholder until you have IBL
    vec3 ambient = vec3(0.03) * albedo;

    vec3 color = ambient + Lo;

    // Reinhard tonemapping + gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    o_color = vec4(color, base.a);
}