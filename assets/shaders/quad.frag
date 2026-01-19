#version 450

layout(set = 0, binding = 1) uniform sampler2D u_textures[32];

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in int v_tex_index;

layout(location = 0) out vec4 outColor;

void main() {
    int idx = clamp(v_tex_index, 0, 31);
    vec4 tex = texture(u_textures[idx], v_uv);
    outColor = tex * v_color;
}