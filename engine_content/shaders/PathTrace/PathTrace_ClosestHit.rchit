#version 460
#extension GL_EXT_ray_tracing           : require
#extension GL_EXT_buffer_reference2     : require
#extension GL_EXT_scalar_block_layout   : require
#extension GL_ARB_gpu_shader_int64      : require

#include "PathTrace/PathTrace_HitRecord.glsl"

layout(location = 0) rayPayloadInEXT HitRecord hit_record;
hitAttributeEXT vec2 barycentrics;

#define VERTEX_STRIDE 6
layout(buffer_reference, scalar) readonly buffer VertexBuffer { float v[]; };
#include "vertex_decode.glsl"
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

    uint base = uint(gl_PrimitiveID) * 3u;
    uint i0 = ib.i[base];
    uint i1 = ib.i[base + 1u];
    uint i2 = ib.i[base + 2u];
    float w0 = 1.0 - barycentrics.x - barycentrics.y;
    float w1 = barycentrics.x;
    float w2 = barycentrics.y;

    // Interpolated vertex normal → world space
    vec3 n0 = vb_unpack_normal(vb.v[i0*VERTEX_STRIDE + 3]);
    vec3 n1 = vb_unpack_normal(vb.v[i1*VERTEX_STRIDE + 3]);
    vec3 n2 = vb_unpack_normal(vb.v[i2*VERTEX_STRIDE + 3]);
    vec3 world_normal = normalize(mat3(gl_ObjectToWorldEXT) * normalize(n0*w0 + n1*w1 + n2*w2));

    // Interpolated tangent → world space
    vec4 t0 = vb_unpack_tangent(vb.v[i0*VERTEX_STRIDE + 4]);
    vec4 t1 = vb_unpack_tangent(vb.v[i1*VERTEX_STRIDE + 4]);
    vec4 t2 = vb_unpack_tangent(vb.v[i2*VERTEX_STRIDE + 4]);
    vec4 obj_tangent4 = t0*w0 + t1*w1 + t2*w2;
    vec3 world_tangent = normalize(mat3(gl_ObjectToWorldEXT) * obj_tangent4.xyz);

    hit_record.barycentrics  = barycentrics;
    hit_record.hit_t         = gl_HitTEXT;
    hit_record.instance_id   = gl_InstanceCustomIndexEXT;
    hit_record.primitive_id  = gl_PrimitiveID;
    hit_record.hit_kind      = 1u;
    hit_record._pad0         = 0.0;
    hit_record._pad1         = 0.0;
    hit_record.shading_normal = vec4(world_normal, 0.0);
    hit_record.tangent        = vec4(world_tangent, obj_tangent4.w);
}