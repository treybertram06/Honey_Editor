#type vertex
#version 450

layout(location = 0) out vec2 v_uv;

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); // (0,0), (2,0), (0,2)
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    v_uv = uv;
}

#type fragment
#version 450

layout(location = 0) in vec2 v_uv;

layout(set = 1, binding = 0) uniform texture2D  u_Scene;
layout(set = 1, binding = 1) uniform sampler    u_LinearClampSampler;

layout(location = 0) out vec4 o_color;

void main() {
    o_color  = texture(sampler2D(u_Scene, u_LinearClampSampler), v_uv);
}
