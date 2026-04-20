// Renderer3D_MeshletDeferred.glsl
// Task/mesh stages are identical to Renderer3D_Meshlet.glsl.
// Fragment stage writes G-buffer outputs (albedo, normal, pbr) instead of
// performing PBR lighting — mirrors Renderer3D_DeferredGeometry.glsl but
// uses v_materialIndex from the mesh stage rather than a push constant.

#type task
#version 460
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 1) in;

struct DrawData {
    mat4  model;
    uint  meshlets_offset;
    uint  meshlet_count;
    int   material_index;
    int   entity_id;
};
layout(set = 1, binding = 5) readonly buffer DrawDataBuffer { DrawData draws[]; };

struct TaskPayload { uint meshlet_id; uint draw_id; };
taskPayloadSharedEXT TaskPayload payload;

layout(push_constant) uniform MeshletPC {
    int u_DrawDataBase;
    int _pad0;
    int _pad1;
    int _pad2;
} u_PC;

void main() {
    uint draw_id = uint(u_PC.u_DrawDataBase) + uint(gl_DrawID);
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

#type mesh
#version 460
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 32) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

layout(set = 1, binding = 0) readonly buffer VB  { float vertices[];          };
layout(set = 1, binding = 1) readonly buffer MB  { uint  meshlets[];          };
layout(set = 1, binding = 2) readonly buffer MVI { uint  meshlet_vertices[];  };
layout(set = 1, binding = 3) readonly buffer MTI { uint  meshlet_triangles[]; };

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

layout(location = 0) out vec2       v_uv0[];
layout(location = 1) out vec2       v_uv1[];
layout(location = 2) out vec3       v_normalWS[];
layout(location = 3) out vec3       v_positionWS[];
layout(location = 4) out vec4       v_tangentWS[];
layout(location = 5) flat out int   v_entityID[];
layout(location = 6) flat out int   v_materialIndex[];

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
        uint base_f = vi * 14u;

        vec3 pos = vec3(vertices[base_f + 0u], vertices[base_f + 1u], vertices[base_f + 2u]);
        vec3 nor = vec3(vertices[base_f + 3u], vertices[base_f + 4u], vertices[base_f + 5u]);
        vec4 tan = vec4(vertices[base_f + 6u], vertices[base_f + 7u], vertices[base_f + 8u], vertices[base_f + 9u]);
        vec2 uv0 = vec2(vertices[base_f + 10u], vertices[base_f + 11u]);
        vec2 uv1 = vec2(vertices[base_f + 12u], vertices[base_f + 13u]);

        gl_MeshVerticesEXT[i].gl_Position = mvp * vec4(pos, 1.0);
        v_positionWS[i]    = (d.model * vec4(pos, 1.0)).xyz;
        v_normalWS[i]      = normalize(normalMat * nor);
        v_tangentWS[i]     = vec4(normalize(normalMat * tan.xyz), tan.w);
        v_uv0[i]           = uv0;
        v_uv1[i]           = uv1;
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

#type fragment
#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2      v_uv0;
layout(location = 1) in vec2      v_uv1;
layout(location = 2) in vec3      v_normalWS;
layout(location = 3) in vec3      v_positionWS;
layout(location = 4) in vec4      v_tangentWS;
layout(location = 5) flat in int  v_entityID;
layout(location = 6) flat in int  v_materialIndex;

// gAlbedo    (RGBA8):   RGB = linear albedo, A = alpha
// gNormal    (RGBA16F): RG = oct-encoded world-space normal, BA = packed emissive
// gPBRParams (RGBA8):   R = metallic, G = roughness, B = AO, A = unused
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

// Sign-preserving octahedral encoding (Cigolle et al.)
vec2 oct_encode(vec3 n) {
    vec2 p = n.xy / (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z <= 0.0)
        p = (1.0 - abs(p.yx)) * vec2(p.x >= 0.0 ? 1.0 : -1.0,
                                     p.y >= 0.0 ? 1.0 : -1.0);
    return p;
}

float pack_rg(float r, float g) {
    return floor(r * 255.0) / 256.0 + g / 256.0;
}

void main() {
    GPUMaterial mat = u_Materials.materials[v_materialIndex];

    vec2 base_uv = apply_uv_transform(select_uv(mat.base_color_uv_set),
                                      mat.base_color_uv_scale_offset,
                                      mat.base_color_uv_rotation);
    vec4 base_tex = sample_texture(mat.base_color_tex_id, base_uv);

    vec3 base_linear = (mat.base_color_tex_id >= 0) ? pow(base_tex.rgb, vec3(2.2)) : vec3(1.0);
    vec3 albedo = base_linear * mat.base_color.rgb;
    float alpha = base_tex.a * mat.base_color.a;

    if (mat.alpha_mode == 1 && alpha < mat.alpha_cutoff)
        discard;

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

    float ao = 1.0;
    if (mat.occlusion_tex_id >= 0) {
        vec2 ao_uv = apply_uv_transform(select_uv(mat.occlusion_uv_set),
                                        mat.occlusion_uv_scale_offset,
                                        mat.occlusion_uv_rotation);
        ao = mix(1.0, sample_texture(mat.occlusion_tex_id, ao_uv).r,
                 clamp(mat.occlusion_strength, 0.0, 1.0));
    }

    vec3 emissive = mat.emissive_factor.rgb;
    if (mat.emissive_tex_id >= 0) {
        vec2 e_uv = apply_uv_transform(select_uv(mat.emissive_uv_set),
                                       mat.emissive_uv_scale_offset,
                                       mat.emissive_uv_rotation);
        emissive *= pow(sample_texture(mat.emissive_tex_id, e_uv).rgb, vec3(2.2));
    }

    o_albedo = vec4(albedo, alpha);

    vec2 enc_n = oct_encode(N);
    o_normal = vec4(enc_n,
                    pack_rg(clamp(emissive.r, 0.0, 1.0), clamp(emissive.g, 0.0, 1.0)),
                    pack_rg(clamp(emissive.b, 0.0, 1.0), 0.0));

    o_pbr = vec4(metallic, roughness, ao, 0.0);
}