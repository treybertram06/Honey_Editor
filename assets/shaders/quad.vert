#version 450

layout(set = 0, binding = 0, std140) uniform CameraUBO {
    mat4 u_ViewProjection;
} u_Camera;

// Set=0, binding=1 is the texture array (used in fragment), declared there.

// Static quad vertex data
layout(location = 0) in vec2 a_local_pos; // (-0.5..0.5)
layout(location = 1) in vec2 a_local_tex; // (0..1)

// Per-instance data (must match Vulkan pipeline vertex input state)
layout(location = 2) in vec3  i_center;
layout(location = 3) in vec2  i_half_size;
layout(location = 4) in float i_rotation;
layout(location = 5) in vec4  i_color;
layout(location = 6) in int   i_tex_index;
layout(location = 7) in float i_tiling;
layout(location = 8) in vec2  i_tex_coord_min;
layout(location = 9) in vec2  i_tex_coord_max;
layout(location = 10) in int  i_entity_id;

// Outputs to fragment
layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) flat out int v_tex_index;

vec2 rotate(vec2 v, float r) {
    float c = cos(r);
    float s = sin(r);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

void main() {
    // Build world position from unit quad
    vec2 local = a_local_pos * (i_half_size * 2.0);
    local = rotate(local, i_rotation);
    vec3 world = i_center + vec3(local, 0.0);

    gl_Position = u_Camera.u_ViewProjection * vec4(world, 1.0);

    // UVs: remap 0..1 into sub-rect then apply tiling
    vec2 uv_rect = mix(i_tex_coord_min, i_tex_coord_max, a_local_tex);
    v_uv = uv_rect * i_tiling;

    v_color = i_color;
    v_tex_index = i_tex_index;

    // Consume entity id so validation stops complaining (useful later for picking)
    // (No output yet; just referencing it prevents it being optimized away in some compilers.)
    if (i_entity_id == -2147483648) {
        v_color *= 0.0;
    }
}