#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 payload;
hitAttributeEXT vec2 barycentrics;

void main() {
    // gl_GeometryIndexEXT  → which BLAS geometry (always 0 for us)
    // gl_PrimitiveID       → triangle index within the BLAS
    // barycentrics         → barycentric coords of hit point
    // gl_WorldRayDirectionEXT → incoming ray direction

    // For normals-only, use the geometric normal from the ray.
    // A real implementation would fetch vertex normals from a bound SSBO.
    // For now, visualize the world-space face normal derived from hit geometry.

    // gl_ObjectToWorldEXT gives object→world transform.
    // gl_WorldRayDirectionEXT is the incident direction.
    // The built-in gl_HitTEXT is the hit distance.

    // Simple: remap the incoming direction's reflection as a stand-in for normal.
    // Replace this with actual vertex normal interpolation once geometry SSBOs are bound.
    vec3 approx_normal = normalize(-gl_WorldRayDirectionEXT);
    payload = approx_normal * 0.5 + 0.5; // remap [-1,1] → [0,1] for visualization
}