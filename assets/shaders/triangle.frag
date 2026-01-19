#version 450

layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(vColor, 1.0);
    //outColor = vec4(1.0, 0.0, 0.0, 1.0); // Solid red color
}