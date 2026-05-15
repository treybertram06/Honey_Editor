// Renderer3D_ShadowDirectional_Classic.glsl
// Classic geometry fallback for cascaded directional shadow maps.
// One render pass per cascade; face_index carries the cascade index.
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

layout(set = 0, binding = 7, std430) readonly buffer DirShadowBuf {
    mat4  cascade_vp[4];
    float cascade_splits[4];
    uint  cascade_count;
    uint  enabled;
    float shadow_distance;
    uint  _pad;
} u_DirShadow;

layout(push_constant) uniform ShadowDrawPC {
    uint draw_data_base; // unused — firstInstance already offsets gl_InstanceIndex
    int  light_index;    // unused — present to match ShadowDrawPC layout (16 bytes)
    uint face_index;     // cascade index
    uint _pad;
} u_PC;

void main() {
    mat4 model = mat4(a_iModel0, a_iModel1, a_iModel2, a_iModel3);
    mat4 mvp   = u_DirShadow.cascade_vp[u_PC.face_index] * model;
    gl_Position = mvp * vec4(a_position, 1.0);
}

#type fragment
#version 450
void main() {}