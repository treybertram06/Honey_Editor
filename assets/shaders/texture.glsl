#type vertex
#version 330 core
layout (location = 0) in vec2 a_local_pos;     // static
layout (location = 1) in vec2 a_local_tex;     // static

layout (location = 2) in vec3 i_center;        // per-instance
layout (location = 3) in vec2 i_half_size;
layout (location = 4) in float i_rotation;
layout (location = 5) in vec4 i_color;
layout (location = 6) in float i_tex_index;
layout (location = 7) in float i_tiling;
layout (location = 8) in vec2 i_tex_coord_min;
layout (location = 9) in vec2 i_tex_coord_max;

uniform mat4 u_view_projection;

out vec2 v_tex_coord;
out vec4 v_color;
out float v_tex_index;
out float v_tiling;

void main()
{
    // scale to quad size
    vec2 p = a_local_pos * i_half_size * 2.0;

    // optional rotation
    if (i_rotation != 0.0) {
        float s = sin(i_rotation), c = cos(i_rotation);
        p = vec2(c*p.x - s*p.y, s*p.x + c*p.y);
    }

    vec3 world_pos = i_center + vec3(p, 0.0);
    gl_Position = u_view_projection * vec4(world_pos, 1.0);
    v_tex_coord  = mix(i_tex_coord_min, i_tex_coord_max, a_local_tex);
    v_color      = i_color;
    v_tex_index  = i_tex_index;
    v_tiling     = i_tiling;
}

#type fragment
#version 330 core
layout(location = 0) out vec4 outColor;

in vec4  v_color;
in vec2  v_tex_coord;
in float v_tex_index;
in float v_tiling;

uniform sampler2D u_textures[MAX_TEXTURE_SLOTS];


void main()
{
    outColor = texture(u_textures[int(v_tex_index)],
                       v_tex_coord * v_tiling) * v_color;
}