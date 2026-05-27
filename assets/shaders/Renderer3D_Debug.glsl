#type vertex
#version 450

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;

#ifdef HN_VULKAN
layout(set = 0, binding = 0, std140) uniform CameraUBO {
    mat4 u_view_projection;
};
#else
layout(std140, binding = 0) uniform CameraUBO {
    mat4 u_view_projection;
};
#endif

layout(location = 0) out vec4 v_color;

void main()
{
    v_color     = a_color;
    gl_Position = u_view_projection * vec4(a_position, 1.0);
}

#type fragment
#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec4 v_color;

void main()
{
    if (v_color.a < 0.01)
        discard;
    outColor = v_color;
}
