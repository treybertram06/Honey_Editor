// Lite tier ubershader: layout-mode descriptors (no heap, no bindless), vertex pulling from SSBOs.
//   mode 0 = lit mesh   (set 1: vertex + flat-index buffers of the mesh)
//   mode 1 = debug line (set 1 binding 0: 28-byte {vec3 pos; vec4 col} vertices, drawn as a line list)
//   mode 2 = icon quad  (two triangles, 6 vertices; pc.model[0] = {world pos xyz, size in pixels}, pc.base_color = tint)
#type vertex
#version 450

layout(set = 0, binding = 0, std140) uniform LiteFrame {
    mat4 view_proj;
    vec4 cam_pos;
    vec4 light_dir;   // xyz = direction the light travels, w = intensity
    vec4 light_color; // rgb
    vec4 ambient;     // rgb
    vec4 viewport;    // xy = render target size in pixels
} u_frame;

layout(set = 1, binding = 0, std430) readonly buffer VB { float vertices[]; };
layout(set = 1, binding = 1, std430) readonly buffer IB { uint  indices[]; };

layout(push_constant) uniform PC {
    mat4 model;
    vec4 base_color;
    uint first_index;
    int  entity_id;
    uint mode;
    uint flags;
} pc;

#include "vertex_decode.glsl"

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec4 v_color;

void main()
{
    v_normal = vec3(0.0, 0.0, 1.0);
    v_uv     = vec2(0.0);
    v_color  = vec4(1.0);

    if (pc.mode == 0u) {
        // VertexPBR: 6 uint32s per vertex (see vertex_decode.glsl)
        uint vi   = indices[pc.first_index + uint(gl_VertexIndex)];
        uint base = vi * 6u;

        vec3 pos = vec3(vertices[base], vertices[base + 1u], vertices[base + 2u]);
        v_normal = normalize(mat3(pc.model) * vb_unpack_normal(vertices[base + 3u]));
        v_uv     = vb_unpack_uv(vertices[base + 5u]);

        gl_Position = u_frame.view_proj * pc.model * vec4(pos, 1.0);
    } else if (pc.mode == 1u) {
        uint base = uint(gl_VertexIndex) * 7u;
        vec3 pos  = vec3(vertices[base], vertices[base + 1u], vertices[base + 2u]);
        v_color   = vec4(vertices[base + 3u], vertices[base + 4u], vertices[base + 5u], vertices[base + 6u]);
        gl_Position = u_frame.view_proj * vec4(pos, 1.0);
    } else {
        // Billboard with a constant on-screen pixel size, built in clip space.
        const uint corners[6] = uint[6](0u, 1u, 2u, 2u, 1u, 3u);
        uint corner = corners[uint(gl_VertexIndex) % 6u];
        v_uv    = vec2(float((corner & 2u) >> 1u), float(corner & 1u));
        v_color = pc.base_color;

        vec4 clip = u_frame.view_proj * vec4(pc.model[0].xyz, 1.0);
        vec2 px   = pc.model[0].w / u_frame.viewport.xy;
        clip.xy  += (v_uv - 0.5) * px * 2.0 * clip.w;
        gl_Position = clip;
    }
}

#type fragment
#version 450

layout(set = 0, binding = 0, std140) uniform LiteFrame {
    mat4 view_proj;
    vec4 cam_pos;
    vec4 light_dir;
    vec4 light_color;
    vec4 ambient;
    vec4 viewport;
} u_frame;

layout(set = 2, binding = 0) uniform sampler2D u_base_color;

layout(push_constant) uniform PC {
    mat4 model;
    vec4 base_color;
    uint first_index;
    int  entity_id;
    uint mode;
    uint flags;
} pc;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec4 v_color;

layout(location = 0) out vec4 o_color;
layout(location = 1) out int  o_entity;

void main()
{
    if (pc.mode == 1u) {
        if (v_color.a < 0.01) discard;
        o_color  = v_color;
        o_entity = -1;
        return;
    }

    if (pc.mode == 2u) {
        // Soft-edged disc; the whole disc writes the entity id so the icon stays clickable.
        float a = 1.0 - smoothstep(0.42, 0.5, length(v_uv - 0.5));
        if (a < 0.01) discard;
        o_color  = vec4(v_color.rgb, v_color.a * a);
        o_entity = pc.entity_id;
        return;
    }

    vec4 albedo = pc.base_color;
    if ((pc.flags & 1u) != 0u)
        albedo *= vec4(pow(texture(u_base_color, v_uv).rgb, vec3(2.2)), 1.0); // textures are stored UNORM, decode to linear like the forward shader

    vec3 N = normalize(v_normal);
    if (!gl_FrontFacing) N = -N;
    float ndl = max(dot(N, -normalize(u_frame.light_dir.xyz)), 0.0);
    vec3 lit = albedo.rgb * (u_frame.ambient.rgb + u_frame.light_color.rgb * u_frame.light_dir.w * ndl);

    lit = lit / (lit + vec3(1.0));   // Reinhard
    lit = pow(lit, vec3(1.0 / 2.2)); // viewport is RGBA8 with no tonemap pass

    o_color  = vec4(lit, albedo.a);
    o_entity = pc.entity_id;
}
