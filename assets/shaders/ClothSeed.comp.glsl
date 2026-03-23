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

    // Seed a flat cloth grid in XZ space, with top row pinned.
    float nx = (pc.width > 1u) ? float(x) / float(pc.width - 1u) : 0.0;
    float ny = (pc.height > 1u) ? float(y) / float(pc.height - 1u) : 0.0;

    Particle p;
    p.pos = vec4((nx - 0.5) * 4.0, 1.5, (ny - 0.5) * 4.0, (y == 0u) ? 0.0 : 1.0);
    p.vel = vec4(0.0, 0.0, 0.0, 0.0);

    outP[idx] = p;
}