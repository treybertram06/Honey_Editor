#type vertex
#version 450


layout (location = 0) in vec2 a_local_pos;     // static
layout (location = 1) in vec2 a_local_tex;     // static

layout (location = 2) in vec3 i_center;        // per-instance
layout (location = 3) in vec2 i_half_size;
layout (location = 4) in float i_rotation;
layout (location = 5) in vec4 i_color;
layout (location = 6) in int i_tex_index;
layout (location = 7) in float i_tiling;
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

layout(location=0) out vec4 v_color;
layout(location=1) out vec2 v_tex_coord;
layout(location=2) flat out int v_tex_index;
layout(location=3) out float v_tiling_factor;
layout(location=4) flat out int v_entity_id;

void main()
{
    vec2 p = a_local_pos * i_half_size * 2.0;

    if (i_rotation != 0.0) {
        float s = sin(i_rotation), c = cos(i_rotation);
        p = vec2(c*p.x - s*p.y, s*p.x + c*p.y);
    }

    vec3 world_pos = i_center + vec3(p, 0.0);

    v_tex_coord         = mix(i_tex_coord_min, i_tex_coord_max, a_local_tex);
    v_color             = i_color;
    v_tex_index         = i_tex_index;
    v_tiling_factor     = i_tiling;
    v_entity_id         = i_entity_id;

    gl_Position = u_view_projection * vec4(world_pos, 1.0);
}

#type fragment
#version 450

layout(location = 0) out vec4 outColor;
layout(location = 1) out int entity_id;

layout(location=0) in vec4 v_color;
layout(location=1) in vec2 v_tex_coord;
layout(location=2) flat in int v_tex_index;
layout(location=3) in float v_tiling_factor;
layout(location=4) flat in int v_entity_id;

#ifdef HN_VULKAN
// binding 1: single sampler
layout (set = 0, binding = 1) uniform sampler u_sampler;
// binding 2: array of texture2D (sampled images only)
layout (set = 0, binding = 2) uniform texture2D u_textures[MAX_TEXTURE_SLOTS];
#else
layout (binding = 0) uniform sampler2D u_textures[MAX_TEXTURE_SLOTS];
#endif

void main() {
#ifdef HN_DEBUG_PICK
    // Visualize ID coverage independent of textures/alpha.
    // (A small hash to turn IDs into colors.)
    int id = v_entity_id;
    float r = fract(sin(float(id) * 12.9898) * 43758.5453);
    float g = fract(sin(float(id) * 78.233)  * 43758.5453);
    float b = fract(sin(float(id) * 39.425)  * 43758.5453);
    outColor = vec4(r, g, b, 1.0);
    entity_id = id;
    return;
#endif
    int idx = clamp(v_tex_index, 0, MAX_TEXTURE_SLOTS - 1);

    vec2 flipped_uv = vec2(v_tex_coord.x, 1.0 - v_tex_coord.y);

#ifdef HN_VULKAN
    // Construct a temporary sampler2D on the fly; no local sampler variables.
    outColor = texture(
                   sampler2D(u_textures[idx], u_sampler),
                   flipped_uv * v_tiling_factor
               ) * v_color;
#else
    outColor = texture(u_textures[idx], v_tex_coord * v_tiling_factor) * v_color;
#endif

    entity_id = v_entity_id;
}