// assets/shaders/basic3d.glsl
#type vertex
#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_tex_coord;

uniform mat4 u_view_projection;
uniform mat4 u_transform;

out vec3 v_normal;
out vec2 v_tex_coord;
out vec3 v_world_pos;

void main() {
v_world_pos = vec3(u_transform * vec4(a_position, 1.0));
v_normal = mat3(transpose(inverse(u_transform))) * a_normal;
v_tex_coord = a_tex_coord;

gl_Position = u_view_projection * u_transform * vec4(a_position, 1.0);
}

#type fragment
#version 330 core

layout(location = 0) out vec4 color;

in vec3 v_normal;
in vec2 v_tex_coord;
in vec3 v_world_pos;

uniform sampler2D u_texture;
uniform vec4 u_color;

void main() {
// Simple lighting
vec3 light_dir = normalize(vec3(1.0, 1.0, 1.0));
vec3 normal = normalize(v_normal);
float diff = max(dot(normal, light_dir), 0.0);

vec3 ambient = vec3(0.3);
vec3 diffuse = vec3(diff);

vec4 tex_color = texture(u_texture, v_tex_coord);
color = vec4((ambient + diffuse), 1.0) * tex_color * u_color;
}