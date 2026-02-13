#type vertex
#version 450

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

// Instance model matrix as 4 vec4s
layout(location = 3) in vec4 a_iModel0;
layout(location = 4) in vec4 a_iModel1;
layout(location = 5) in vec4 a_iModel2;
layout(location = 6) in vec4 a_iModel3;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 u_ViewProjection;
} u_Camera;

layout(location = 0) out vec2 v_uv;

void main() {
    v_uv = a_uv;
    mat4 model = mat4(a_iModel0, a_iModel1, a_iModel2, a_iModel3);
    //mat4 model = mat4(1.0);
    gl_Position = u_Camera.u_ViewProjection * model * vec4(a_position, 1.0);
}

#type fragment
#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

// Globals: sampler + texture array (matches your Vulkan set=0 bindings)
layout(set = 0, binding = 1) uniform sampler u_Sampler;
layout(set = 0, binding = 2) uniform texture2D u_Textures[32];

layout(push_constant) uniform MaterialPC {
    vec4 u_BaseColorFactor;  // 16 bytes
    int  u_BaseColorTexIndex; // 4 bytes
    // Padding to keep std430-like alignment happy across drivers
    int  _pad0;
    int  _pad1;
    int  _pad2;
} u_Material;

void main() {
    int idx = clamp(u_Material.u_BaseColorTexIndex, 0, 31);
    vec4 base = texture(sampler2D(u_Textures[idx], u_Sampler), v_uv);
    o_color = base * u_Material.u_BaseColorFactor;
}