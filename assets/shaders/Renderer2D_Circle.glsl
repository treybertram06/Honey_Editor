#type vertex
#version 410 core
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec2 a_local_pos;     // static
layout (location = 1) in vec2 a_local_tex;     // static

layout (location = 2) in vec3 i_center;        // per-instance
layout (location = 3) in vec2 i_half_size;
layout (location = 4) in float i_thickness;
layout (location = 5) in vec4 i_color;
layout (location = 6) in float i_fade;
layout (location = 7) in int i_tex_index;
layout (location = 8) in vec2 i_tex_coord_min;
layout (location = 9) in vec2 i_tex_coord_max;
layout (location = 10) in int i_entity_id;

layout (std140, binding = 0) uniform camera {
mat4 u_view_projection;
};

layout(location=0) out vec4 v_color;
layout(location=1) out vec2 v_tex_coord;
layout(location=2) flat out int v_tex_index;
layout(location=3) out vec2 v_local_pos;     // for circle mask
layout(location=4) out float v_thickness;
layout(location=5) out float v_fade;
layout(location=6) flat out int v_entity_id;

void main()
{
    // same quad expansion as your quad shader
    vec2 p = a_local_pos * i_half_size * 2.0;
    vec3 world_pos = i_center + vec3(p, 0.0);

    v_local_pos = a_local_pos * 2.0; // [-1,1] range
    v_tex_coord = mix(i_tex_coord_min, i_tex_coord_max, a_local_tex);

    v_color     = i_color;
    v_tex_index = i_tex_index;
    v_thickness = i_thickness;
    v_fade      = i_fade;
    v_entity_id = i_entity_id;

    gl_Position = u_view_projection * vec4(world_pos, 1.0);
}

#type fragment
#version 410 core
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) out vec4 outColor;
layout(location = 1) out int entity_id;

layout(location=0) in vec4 v_color;
layout(location=1) in vec2 v_tex_coord;
layout(location=2) flat in int v_tex_index;
layout(location=3) in vec2 v_local_pos;
layout(location=4) in float v_thickness;
layout(location=5) in float v_fade;
layout(location=6) flat in int v_entity_id;

layout (binding = 0) uniform sampler2D u_textures[MAX_TEXTURE_SLOTS];

void main()
{
    // r==1.0 at circle edge
    float r = length(v_local_pos);

    // Outer edge AA: 1 inside, 0 outside
    float outer = 1.0 - smoothstep(1.0 - v_fade, 1.0, r);

    // thickness: 1.0 -> filled, smaller -> ring
    float t = clamp(v_thickness, 0.0, 1.0);
    float innerRadius = 1.0 - t;

    // If innerRadius > 0 => cut out the middle (ring)
    float hasHole = step(0.0001, innerRadius);
    float holeCut = smoothstep(innerRadius, innerRadius + v_fade, r); // 0 in hole, 1 outside
    float ring = mix(1.0, holeCut, hasHole);

    float alpha = outer * ring;
    if (alpha <= 0.0)
    discard;

    vec4 tex = texture(u_textures[v_tex_index], v_tex_coord);
    outColor = vec4(tex.rgb * v_color.rgb, tex.a * v_color.a * alpha);
    entity_id = v_entity_id;
}
