#version 460
#extension GL_EXT_ray_tracing : require

// Must match RayGen and ClosestHit shaders exactly
struct HitPayload {
    vec3  radiance;
    float _pad0;
    vec3  throughput;
    float _pad1;
    vec3  next_origin;
    float _pad2;
    vec3  next_direction;
    uint  seed;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() {
    payload.radiance       = vec3(0.05, 0.05, 0.08); // dim sky contribution
    payload.next_direction = vec3(0.0);               // signals path termination to raygen
}