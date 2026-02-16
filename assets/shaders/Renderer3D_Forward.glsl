#type vertex
#version 450

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

// Instance model matrix as 4 vec4s
layout(location = 3) in vec4 a_iModel0;
layout(location = 4) in vec4 a_iModel1;
layout(location = 5) in vec4 a_iModel2;
layout(location = 6) in vec4 a_iModel3;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 u_ViewProjection;
} u_Camera;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec3 v_normalWS;

void main() {
    v_uv = a_uv;

    mat4 model = mat4(a_iModel0, a_iModel1, a_iModel2, a_iModel3);

    // Transform normal to world space.
    // Using inverse-transpose handles non-uniform scaling correctly.
    mat3 normalMat = transpose(inverse(mat3(model)));
    v_normalWS = normalize(normalMat * a_normal);

    gl_Position = u_Camera.u_ViewProjection * model * vec4(a_position, 1.0);
}

#type fragment
#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_normalWS;

layout(location = 0) out vec4 o_color;

// Globals: sampler + texture array (matches your Vulkan set=0 bindings)
layout(set = 0, binding = 1) uniform sampler u_Sampler;
layout(set = 0, binding = 2) uniform texture2D u_Textures[32];

layout(push_constant) uniform MaterialPC {
    vec4 u_BaseColorFactor;   // 16 bytes
    int  u_BaseColorTexIndex; // 4 bytes
    int  _pad0;
    int  _pad1;
    int  _pad2;
} u_Material;

void main() {
    int idx = clamp(u_Material.u_BaseColorTexIndex, 0, 31);
    vec4 base = texture(sampler2D(u_Textures[nonuniformEXT(idx)], u_Sampler), v_uv);

    // --- Basic hardcoded lighting (no extra engine inputs) ---
    vec3 N = normalize(v_normalWS);

    // Direction *towards* the surface (pick whatever looks nice)
    vec3 L = normalize(vec3(0.4, 0.8, 0.2));

    float ndotl = max(dot(N, L), 0.0);

    vec3 ambient = vec3(0.3);          // small constant ambient
    vec3 diffuse = vec3(1.0) * ndotl;   // white directional light

    vec3 lit = base.rgb * (ambient + diffuse);

    o_color = vec4(lit, base.a);
}