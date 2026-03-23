#type compute
#version 450
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

struct Particle {
    vec4 pos; // xyz + invMass
    vec4 vel; // xyz + pad
};

layout(set = 0, binding = 0) readonly buffer InState  { Particle inP[]; };
layout(set = 0, binding = 1) writeonly buffer OutState { Particle outP[]; };

layout(push_constant) uniform SimPC {
    float dt;
    uint width;
    uint height;
    uint frameIndex;
} pc;

void main() {
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;
    if (x >= pc.width || y >= pc.height) return;
    uint idx = y * pc.width + x;

    Particle p = inP[idx];
    // TODO: forces/constraints/integration
    p.vel.xyz += vec3(0.0, -9.81, 0.0) * pc.dt;
    p.pos.xyz += p.vel.xyz * pc.dt;
    outP[idx] = p;
}