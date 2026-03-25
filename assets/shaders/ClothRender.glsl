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

void main() {
    // Simple grid-shading based on world-space height
    float t = clamp(v_pos_ws.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 col_top    = vec3(0.70, 0.78, 0.92);
    vec3 col_bottom = vec3(0.35, 0.40, 0.55);
    o_color = vec4(mix(col_bottom, col_top, t), 1.0);
}
