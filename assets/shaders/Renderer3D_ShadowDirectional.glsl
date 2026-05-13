// Renderer3D_ShadowDirectional.glsl
// Fragment stage is empty — depth is written automatically by the rasterizer.

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

layout(push_constant) uniform ShadowDrawPC {
    uint draw_data_base;
    int  light_index; // unused — present to match ShadowDrawPC layout (16 bytes)
    uint face_index;  // unused
    uint _pad;
} u_PC;

void main() {
    uint draw_id  = u_PC.draw_data_base + gl_DrawID;
    DrawData d    = draws[draw_id];
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

layout(set = 1, binding = 0) readonly buffer VB  { float vertices[];         };
layout(set = 1, binding = 1) readonly buffer MB  { uint  meshlets[];         };
layout(set = 1, binding = 2) readonly buffer MVI { uint  meshlet_vertices[]; };
layout(set = 1, binding = 3) readonly buffer MTI { uint  meshlet_triangles[];};

struct DrawData {
    mat4  model;
    uint  meshlets_offset;
    uint  meshlet_count;
    int   material_index;
    int   entity_id;
};
layout(set = 1, binding = 5) readonly buffer DrawDataBuffer { DrawData draws[]; };

layout(set = 0, binding = 7, std430) readonly buffer DirShadowBuf {
    mat4     cascade_vp[4];       // one per cascade
    float    cascade_splits[4];
    uint     cascade_count;
    uint     enabled;
    float    shadow_distance;
    uint     _pad;
} u_DirShadow;

layout(push_constant) uniform ShadowDrawPC {
    uint draw_data_base;
    int  light_index; // unused — present to match ShadowDrawPC layout (16 bytes)
    uint face_index;  // unused
    uint _pad;
} u_PC;

struct TaskPayload { uint meshlet_id; uint draw_id; };
taskPayloadSharedEXT TaskPayload payload;

#define MESHLET_STRIDE 4u
#define VERTEX_STRIDE  14u

void main() {
    DrawData d  = draws[payload.draw_id];
    uint mi     = payload.meshlet_id;

    uint base      = mi * MESHLET_STRIDE;
    uint vtx_off   = meshlets[base + 0u];
    uint tri_off   = meshlets[base + 1u];
    uint vtx_count = meshlets[base + 2u];
    uint tri_count = meshlets[base + 3u];

    SetMeshOutputsEXT(vtx_count, tri_count);

    mat4 mvp = u_DirShadow.cascade_vp[u_PC.face_index] * d.model;

    uint tid = gl_LocalInvocationID.x;

    for (uint vi = tid; vi < vtx_count; vi += 32u) {
        uint idx  = meshlet_vertices[vtx_off + vi];
        uint base = idx * VERTEX_STRIDE;
        vec3 pos  = vec3(vertices[base + 0u], vertices[base + 1u], vertices[base + 2u]);
        gl_MeshVerticesEXT[vi].gl_Position = mvp * vec4(pos, 1.0);
    }

    for (uint ti = tid; ti < tri_count; ti += 32u) {
        uint byte0 = tri_off + ti * 3u;
        uint byte1 = byte0 + 1u;
        uint byte2 = byte0 + 2u;
        uint a = (meshlet_triangles[byte0 / 4u] >> ((byte0 % 4u) * 8u)) & 0xFFu;
        uint b = (meshlet_triangles[byte1 / 4u] >> ((byte1 % 4u) * 8u)) & 0xFFu;
        uint c = (meshlet_triangles[byte2 / 4u] >> ((byte2 % 4u) * 8u)) & 0xFFu;
        gl_PrimitiveTriangleIndicesEXT[ti] = uvec3(a, b, c);
    }
}

#type fragment
#version 460
void main() {}