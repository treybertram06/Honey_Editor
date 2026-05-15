// Renderer3D_ShadowCubemap_Classic.glsl
// Classic geometry fallback for point light shadow cubemaps.
// One render pass per cubemap face; face_index selects the view-projection matrix.
// Fragment stage is empty — depth is written automatically by the rasterizer.
//
// Vertex binding 0 (per-vertex):  mesh vertex buffer, stride = 14 floats (56 bytes)
// Vertex binding 1 (per-instance): instance buffer (InstanceData), stride = 68 bytes
// All per-vertex attributes must be declared to produce the correct binding stride;
// only a_position and the instance model matrix are actually used.

#type vertex
#version 450

// Per-vertex (binding 0, stride 56 bytes)
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
layout(location = 3) in vec2 a_uv0;
layout(location = 4) in vec2 a_uv1;

// Per-instance (binding 1, stride 68 bytes) — prefix a_i marks instanced rate
layout(location = 5) in vec4 a_iModel0;
layout(location = 6) in vec4 a_iModel1;
layout(location = 7) in vec4 a_iModel2;
layout(location = 8) in vec4 a_iModel3;
layout(location = 9) in int  a_iEntityID;

struct ShadowLightMatrices {
    mat4  face_view_proj[6];
    vec3  position;
    float range;
};
layout(set = 0, binding = 6, std430) readonly buffer ShadowBuf {
    uint                shadow_light_count;
    uint                shadow_light_point_indices[8];
    uint                _pad[3];
    ShadowLightMatrices lights[];
} u_Shadow;

layout(push_constant) uniform ShadowDrawPC {
    uint draw_data_base; // unused — firstInstance already offsets gl_InstanceIndex
    int  light_index;
    uint face_index;
    uint _pad;
} u_PC;

void main() {
    mat4 model = mat4(a_iModel0, a_iModel1, a_iModel2, a_iModel3);
    mat4 mvp   = u_Shadow.lights[u_PC.light_index].face_view_proj[u_PC.face_index] * model;
    gl_Position = mvp * vec4(a_position, 1.0);
}

#type fragment
#version 450
void main() {}