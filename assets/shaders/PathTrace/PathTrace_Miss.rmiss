#version 460
#extension GL_EXT_ray_tracing : require

#include "PathTrace/PathTrace_HitRecord.glsl"

layout(location = 0) rayPayloadInEXT HitRecord hit_record;

void main() {
    hit_record.hit_kind = 0; // Miss
}