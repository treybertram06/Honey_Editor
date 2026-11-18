#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT bool shadow_hit;

void main() {
    shadow_hit = false; // Ray escaped — not occluded
}