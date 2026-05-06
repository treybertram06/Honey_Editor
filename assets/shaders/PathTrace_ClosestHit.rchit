#version 460
#extension GL_EXT_ray_tracing           : require
#extension GL_EXT_buffer_reference2     : require
#extension GL_EXT_scalar_block_layout   : require
#extension GL_ARB_gpu_shader_int64      : require

layout(location = 0) rayPayloadInEXT vec3 payload;
hitAttributeEXT vec2 barycentrics;

// VertexPBR layout (56 bytes = 14 floats):
//   [0..2]  position  (vec3)
//   [3..5]  normal    (vec3)
//   [6..9]  tangent   (vec4)
//   [10..11] uv0      (vec2)
//   [12..13] uv1      (vec2)
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

void main() {
    GeometryInfo geo = u_geometry.entries[gl_InstanceCustomIndexEXT];
    VertexBuffer vb  = VertexBuffer(geo.vertex_buf_addr);
    IndexBuffer  ib  = IndexBuffer(geo.index_buf_addr);

    uint base = uint(gl_PrimitiveID) * 3;
    uint i0 = ib.i[base + 0];
    uint i1 = ib.i[base + 1];
    uint i2 = ib.i[base + 2];

    // Fetch normals (offset 3 in 14-float vertex)
    vec3 n0 = vec3(vb.v[i0 * VERTEX_STRIDE + 3],
                   vb.v[i0 * VERTEX_STRIDE + 4],
                   vb.v[i0 * VERTEX_STRIDE + 5]);
    vec3 n1 = vec3(vb.v[i1 * VERTEX_STRIDE + 3],
                   vb.v[i1 * VERTEX_STRIDE + 4],
                   vb.v[i1 * VERTEX_STRIDE + 5]);
    vec3 n2 = vec3(vb.v[i2 * VERTEX_STRIDE + 3],
                   vb.v[i2 * VERTEX_STRIDE + 4],
                   vb.v[i2 * VERTEX_STRIDE + 5]);

    float w0 = 1.0 - barycentrics.x - barycentrics.y;
    vec3 obj_normal = normalize(n0 * w0 + n1 * barycentrics.x + n2 * barycentrics.y);

    // Transform to world space
    vec3 world_normal = normalize(mat3(gl_ObjectToWorldEXT) * obj_normal);

    payload = world_normal * 0.5 + 0.5; // visualize as color
}