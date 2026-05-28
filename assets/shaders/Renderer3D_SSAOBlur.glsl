#type vertex
#version 450

layout(location = 0) out vec2 v_uv;

// Fullscreen triangle — no vertex buffer needed.
// gl_VertexIndex 0,1,2 produce a triangle that covers the entire NDC clip space.
void main() {
    vec2 positions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
    );
    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);
    // NDC [-1,1] -> UV [0,1]. Vulkan NDC Y points down, texture V also increases down.
    v_uv = pos * 0.5 + 0.5;
}

#type fragment
#version 450

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 o_color;

layout(set=1, binding=0) uniform sampler2D u_SSAO;

void main() {
    vec2 texel_size = 1.0 / vec2(textureSize(u_SSAO, 0));
    float result = 0.0;
    for (int x = -2; x <= 2; ++x)
    for (int y = -2; y <= 2; ++y)
    result += texture(u_SSAO, v_uv + vec2(x, y) * texel_size).r;
    o_color = vec4(vec3(result / 25.0), 1.0);
}


