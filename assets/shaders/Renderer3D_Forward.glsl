#type vertex
#version 450

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 u_ViewProjection;
} u_Camera;

layout(push_constant) uniform Push {
    mat4 u_Model;
} pc;

layout(location = 0) out vec2 v_uv;

void main() {
    v_uv = a_uv;
    gl_Position = u_Camera.u_ViewProjection * pc.u_Model * vec4(a_position, 1.0);
}

#type fragment
#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() {
    o_color = vec4(v_uv, 0.0, 1.0);
}