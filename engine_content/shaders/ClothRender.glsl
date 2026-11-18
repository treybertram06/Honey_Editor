#type vertex
#version 450

struct Particle {
    vec4 pos; // xyz + invMass
    vec4 vel; // xyz + padding
};

layout(set = 0, binding = 0) readonly buffer ClothState {
    Particle particles[];
} u_cloth;

layout(push_constant) uniform PC {
    mat4 vp;
} pc;

layout(location = 0) out vec3 v_pos_ws;

void main() {
    vec3 p = u_cloth.particles[gl_VertexIndex].pos.xyz;
    v_pos_ws = p;
    gl_Position = pc.vp * vec4(p, 1.0);
}

#type fragment
#version 450

layout(location = 0) in vec3 v_pos_ws;

layout(location = 0) out vec4 o_color;

vec3 hsv2rgb(float h, float s, float v) {
    vec3 k = mod(vec3(5.0, 3.0, 1.0) + h * 6.0, 6.0);
    return v - v * s * clamp(min(k, 4.0 - k), 0.0, 1.0);
}

float hash(uint n) {
    n = (n ^ 61u) ^ (n >> 16u);
    n *= 9u;
    n ^= n >> 4u;
    n *= 0x27d4eb2du;
    n ^= n >> 15u;
    return float(n) / float(0xFFFFFFFFu);
}

void main() {
    float hue = hash(uint(gl_PrimitiveID));
    vec3 col = hsv2rgb(hue, 0.75, 0.90);
    o_color = vec4(col, 1.0);
}