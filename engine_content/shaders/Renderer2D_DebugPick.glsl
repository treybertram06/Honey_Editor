#type vertex
#version 450

layout (location = 0) in vec2 a_local_pos;     // static
layout (location = 1) in vec2 a_local_tex;     // static (unused, but kept for VA compatibility)

layout (location = 2) in vec3 i_center;        // per-instance
layout (location = 3) in vec2 i_half_size;
layout (location = 4) in float i_rotation;
layout (location = 5) in vec4 i_color;         // unused
layout (location = 6) in int i_tex_index;      // unused
layout (location = 7) in float i_tiling;       // unused
layout (location = 8) in vec2 i_tex_coord_min; // unused
layout (location = 9) in vec2 i_tex_coord_max; // unused
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

layout(location=0) flat out int v_entity_id;

void main()
{
    vec2 p = a_local_pos * i_half_size * 2.0;

    if (i_rotation != 0.0) {
        float s = sin(i_rotation), c = cos(i_rotation);
        p = vec2(c*p.x - s*p.y, s*p.x + c*p.y);
    }

    vec3 world_pos = i_center + vec3(p, 0.0);
    v_entity_id = i_entity_id;

    gl_Position = u_view_projection * vec4(world_pos, 1.0);
}

#type fragment
#version 450

layout(location = 0) out vec4 outColor;
layout(location = 1) out int entity_id;

layout(location=0) flat in int v_entity_id;

// A tiny deterministic hash -> RGB in [0,1]
vec3 id_to_rgb(int id)
{
    float f = float(id);
    float r = fract(sin(f * 12.9898) * 43758.5453);
    float g = fract(sin(f * 78.2330) * 43758.5453);
    float b = fract(sin(f * 39.4250) * 43758.5453);
    return vec3(r, g, b);
}

void main()
{
    int id = v_entity_id;

    // Optional: show "no entity" as background-ish
    if (id < 0) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        entity_id = id;
        return;
    }

    outColor = vec4(id_to_rgb(id), 1.0);
    entity_id = id; // keep attachment 1 consistent (harmless)
}