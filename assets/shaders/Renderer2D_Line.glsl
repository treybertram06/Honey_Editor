#type vertex
#version 450

layout (location = 0) in vec2 a_local_pos;     // static quad [-0.5, 0.5]
layout (location = 1) in vec2 a_local_tex;

layout (location = 2) in vec3 i_center;        // per-instance
layout (location = 3) in vec2 i_half_size;
layout (location = 4) in float i_rotation;
layout (location = 5) in vec4 i_color;
layout (location = 6) in float i_fade;
layout (location = 7) in int i_tex_index;
layout (location = 8) in vec2 i_tex_coord_min;
layout (location = 9) in vec2 i_tex_coord_max;
layout (location = 10) in int i_entity_id;

#ifdef HN_VULKAN
layout (set = 0, binding = 0, std140) uniform camera {
    mat4 u_view_projection;
};
#else
layout (std140, binding = 0) uniform camera {
    mat4 u_view_projection;
};
#endif

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_tex_coord;
layout(location = 2) flat out int v_tex_index;
layout(location = 3) out vec2 v_local_pos;     // for edge fade
layout(location = 4) out float v_fade;
layout(location = 5) flat out int v_entity_id;
layout(location = 6) out vec2 v_half_size;

void main()
{
    vec2 p = a_local_pos * i_half_size * 2.0;

    if (i_rotation != 0.0) {
        float s = sin(i_rotation);
        float c = cos(i_rotation);
        p = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
    }

    vec3 world_pos = i_center + vec3(p, 0.0);

    v_local_pos = a_local_pos * 2.0; // [-1, 1]
    v_tex_coord = mix(i_tex_coord_min, i_tex_coord_max, a_local_tex);

    v_color     = i_color;
    v_tex_index = i_tex_index;
    v_fade      = i_fade;
    v_entity_id = i_entity_id;
    v_half_size = i_half_size;

    gl_Position = u_view_projection * vec4(world_pos, 1.0);
}

#type fragment
#version 450

layout(location = 0) out vec4 outColor;
layout(location = 1) out int entity_id;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_tex_coord;
layout(location = 2) flat in int v_tex_index;
layout(location = 3) in vec2 v_local_pos;
layout(location = 4) in float v_fade;
layout(location = 5) flat in int v_entity_id;
layout(location = 6) in vec2 v_half_size;

#ifdef HN_VULKAN
layout (set = 0, binding = 1) uniform sampler u_sampler;
layout (set = 0, binding = 2) uniform texture2D u_textures[MAX_TEXTURE_SLOTS];
#else
layout (binding = 0) uniform sampler2D u_textures[MAX_TEXTURE_SLOTS];
#endif

void main()
{
    float dist_x = (1.0 - abs(v_local_pos.x)) * v_half_size.x;
    float dist_y = (1.0 - abs(v_local_pos.y)) * v_half_size.y;
    float min_dist = min(dist_x, dist_y);

    float alpha = smoothstep(0.0, max(v_fade, 0.0001), min_dist);
    if (alpha <= 0.0)
        discard;

    int idx = clamp(v_tex_index, 0, MAX_TEXTURE_SLOTS - 1);

#ifdef HN_VULKAN
    vec4 tex = texture(sampler2D(u_textures[idx], u_sampler), v_tex_coord);
#else
    vec4 tex = texture(u_textures[idx], v_tex_coord);
#endif

    outColor = vec4(tex.rgb * v_color.rgb, tex.a * v_color.a * alpha);
    entity_id = v_entity_id;
}