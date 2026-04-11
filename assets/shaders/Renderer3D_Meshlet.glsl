// Renderer3D_Meshlet.glsl
// Task shader:     reads DrawData via gl_DrawID, dispatches one mesh group per visible meshlet
// Mesh shader:     reads from 6 SSBOs at set=1, outputs PBR varyings
// Fragment shader: full Cook-Torrance PBR, matches Renderer3D_Forward
//
// Per-draw data (model matrix, meshlet offsets, material, entity) lives in DrawDataBuffer
// at set=1 binding=5, indexed by gl_DrawID — no push constants used.
//
// Vertex buffer layout (VertexPNUV, 32 bytes / 8 floats):
//   float[0..2] = position,  float[3..5] = normal,  float[6..7] = uv
//
// Triangle index buffer: meshoptimizer packs uint8 indices into uint32 words.

// ---------------------------------------------------------------------------
// Shared DrawData struct — identical in task, mesh (fragment reads via varying)
// ---------------------------------------------------------------------------
// (repeated in each stage because GLSL has no includes)

// ---------------------------------------------------------------------------
// Task shader
// ---------------------------------------------------------------------------

#type task
#version 460
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 1) in;

struct DrawData {
    mat4  model;
    uint  meshlets_offset; // index into global meshlets[] for this draw's first meshlet
    uint  meshlet_count;
    int   material_index;
    int   entity_id;
};
layout(set = 1, binding = 5) readonly buffer DrawDataBuffer { DrawData draws[]; };

struct TaskPayload { uint meshlet_id; uint draw_id; };
taskPayloadSharedEXT TaskPayload payload;

void main() {
    uint draw_id = uint(gl_DrawID);
    DrawData d = draws[draw_id];
    uint local_id = gl_GlobalInvocationID.x;

    if (local_id >= d.meshlet_count) {
        EmitMeshTasksEXT(0, 1, 1);
        return;
    }

    payload.meshlet_id = d.meshlets_offset + local_id;
    payload.draw_id    = draw_id;
    EmitMeshTasksEXT(1, 1, 1);
}

// ---------------------------------------------------------------------------
// Mesh shader
// ---------------------------------------------------------------------------

#type mesh
#version 460
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 32) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

// set=1: per-mesh global geometry SSBOs
layout(set = 1, binding = 0) readonly buffer VB  { float vertices[];          };
layout(set = 1, binding = 1) readonly buffer MB  { uint  meshlets[];          };
layout(set = 1, binding = 2) readonly buffer MVI { uint  meshlet_vertices[];  };
layout(set = 1, binding = 3) readonly buffer MTI { uint  meshlet_triangles[]; };
// binding 4 = bounds (reserved for culling)

struct DrawData {
    mat4  model;
    uint  meshlets_offset;
    uint  meshlet_count;
    int   material_index;
    int   entity_id;
};
layout(set = 1, binding = 5) readonly buffer DrawDataBuffer { DrawData draws[]; };

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4  u_ViewProjection;
    vec3  u_Position;
    float _pad0;
} u_Camera;

struct TaskPayload { uint meshlet_id; uint draw_id; };
taskPayloadSharedEXT TaskPayload payload;

layout(location = 0) out vec2       v_uv[];
layout(location = 1) out vec3       v_normalWS[];
layout(location = 2) out vec3       v_positionWS[];
layout(location = 3) flat out int   v_entityID[];
layout(location = 4) flat out int   v_materialIndex[];

#define MESHLET_STRIDE 4u

void main() {
    DrawData d = draws[payload.draw_id];
    uint mi = payload.meshlet_id;

    uint base      = mi * MESHLET_STRIDE;
    uint vtx_off   = meshlets[base + 0u];
    uint tri_off   = meshlets[base + 1u];
    uint vtx_count = meshlets[base + 2u];
    uint tri_count = meshlets[base + 3u];

    SetMeshOutputsEXT(vtx_count, tri_count);

    mat4 mvp       = u_Camera.u_ViewProjection * d.model;
    mat3 normalMat = transpose(inverse(mat3(d.model)));

    for (uint i = gl_LocalInvocationID.x; i < vtx_count; i += 32u) {
        uint vi     = meshlet_vertices[vtx_off + i];
        uint base_f = vi * 8u;
        vec3 pos = vec3(vertices[base_f + 0u], vertices[base_f + 1u], vertices[base_f + 2u]);
        vec3 nor = vec3(vertices[base_f + 3u], vertices[base_f + 4u], vertices[base_f + 5u]);
        vec2 uv  = vec2(vertices[base_f + 6u], vertices[base_f + 7u]);

        gl_MeshVerticesEXT[i].gl_Position = mvp * vec4(pos, 1.0);
        v_positionWS[i]    = (d.model * vec4(pos, 1.0)).xyz;
        v_normalWS[i]      = normalMat * nor;
        v_uv[i]            = uv;
        v_entityID[i]      = d.entity_id;
        v_materialIndex[i] = d.material_index;
    }

    for (uint i = gl_LocalInvocationID.x; i < tri_count; i += 32u) {
        uint byte0 = tri_off + i * 3u;
        uint byte1 = byte0 + 1u;
        uint byte2 = byte0 + 2u;

        uint a = (meshlet_triangles[byte0 / 4u] >> ((byte0 % 4u) * 8u)) & 0xFFu;
        uint b = (meshlet_triangles[byte1 / 4u] >> ((byte1 % 4u) * 8u)) & 0xFFu;
        uint c = (meshlet_triangles[byte2 / 4u] >> ((byte2 % 4u) * 8u)) & 0xFFu;

        gl_PrimitiveTriangleIndicesEXT[i] = uvec3(a, b, c);
    }
}

// ---------------------------------------------------------------------------
// Fragment shader — matches Renderer3D_Forward exactly
// ---------------------------------------------------------------------------

#type fragment
#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2      v_uv;
layout(location = 1) in vec3      v_normalWS;
layout(location = 2) in vec3      v_positionWS;
layout(location = 3) flat in int  v_entityID;
layout(location = 4) flat in int  v_materialIndex;

layout(location = 0) out vec4 o_color;
layout(location = 1) out int  o_entityID;

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
    float metallic;
    float roughness;
    int   base_color_tex_index;
    int   _pad;
};
layout(set = 0, binding = 2) readonly buffer MaterialBuffer {
    GPUMaterial materials[];
} u_Materials;

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

    vec3  num      = NDF * G * F;
    float denom    = 4.0 * NdotV * NdotL + 0.001;
    vec3  specular = num / denom;

    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

void main() {
    GPUMaterial mat = u_Materials.materials[v_materialIndex];

    int  idx  = max(mat.base_color_tex_index, 0);
    vec4 base = texture(sampler2D(u_Textures[nonuniformEXT(idx)], u_Sampler), v_uv)
                * mat.base_color;

    float metallic  = mat.metallic;
    float roughness = mat.roughness;

    vec3 albedo = pow(base.rgb, vec3(2.2));
    vec3 N      = normalize(v_normalWS);
    vec3 V      = normalize(u_Camera.u_CameraPos - v_positionWS);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 Lo = vec3(0.0);

    if (u_Lights.u_DirectionalLight.intensity > 0.0) {
        vec3 L        = normalize(-u_Lights.u_DirectionalLight.direction);
        vec3 radiance = u_Lights.u_DirectionalLight.color
                        * u_Lights.u_DirectionalLight.intensity;
        Lo += brdf(N, V, L, albedo, metallic, roughness, F0, radiance);
    }

    int count = u_Lights.u_DirectionalLight.point_light_count;
    for (int i = 0; i < count; ++i) {
        PointLight pl  = u_Lights.u_PointLights[i];
        vec3  delta    = pl.position - v_positionWS;
        float dist     = length(delta);
        if (dist >= pl.range) continue;

        vec3  L           = normalize(delta);
        float attenuation = 1.0 / (dist * dist);
        float window      = pow(max(1.0 - pow(dist / pl.range, 4.0), 0.0), 2.0);
        vec3  radiance    = pl.color * pl.intensity * attenuation * window;
        Lo += brdf(N, V, L, albedo, metallic, roughness, F0, radiance);
    }

    vec3 ambient = vec3(0.03) * albedo;
    vec3 color   = ambient + Lo;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    o_color    = vec4(color, base.a);
    o_entityID = v_entityID;
}