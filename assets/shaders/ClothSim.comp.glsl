#type compute
#version 450
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// ---------------------------------------------------------------------------
// Simulation constants — tweak these to change cloth behaviour
// ---------------------------------------------------------------------------

// Gravitational acceleration (m/s²). Increase for a heavier feel.
const float GRAVITY = 9.81;

// World-space width/height of the cloth grid in metres.
// Rest length between adjacent particles is derived from this.
const float CLOTH_WORLD_SIZE = 4.0;

// Velocity damping rate in 1/seconds. Frame-rate and substep independent.
// Velocity retains exp(-DAMPING_RATE) of its magnitude each second.
// ~1.0 = light (~63% loss/s), ~3.0 = heavy (~95% loss/s).
const float DAMPING_RATE = 10.5;

// Maximum particle speed (m/s). Particles faster than this are clamped.
// Guards against instability when large constraint corrections first
// propagate to particles that have been in free fall for many frames.
const float MAX_SPEED = 1000.0;

// Minimum neighbour distance before a constraint is skipped (avoids ÷0).
const float CONSTRAINT_EPSILON = 1e-6;

// Epsilon added to the combined inverse-mass denominator (avoids ÷0 when
// both particles are pinned, i.e. invMass == 0 on both sides).
const float MASS_EPSILON = 1e-8;

// ---------------------------------------------------------------------------

struct Particle {
    vec4 pos; // xyz + invMass
    vec4 vel; // xyz + pad
};

layout(set = 0, binding = 0) readonly  buffer InState  { Particle inP[];  };
layout(set = 0, binding = 1) writeonly buffer OutState { Particle outP[]; };

layout(push_constant) uniform SimPC {
    float dt;
    uint  width;
    uint  height;
    uint  frameIndex;
} pc;

void main() {
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;
    if (x >= pc.width || y >= pc.height) return;
    uint idx = y * pc.width + x;

    Particle p = inP[idx];

    // Pinned particles (invMass == 0): keep fixed.
    if (p.pos.w <= 0.0) {
        p.vel.xyz = vec3(0.0);
        outP[idx] = p;
        return;
    }

    // Guard: skip integration when dt is zero to avoid NaN from division.
    if (pc.dt <= 0.0) {
        outP[idx] = p;
        return;
    }

    // --- 1. Verlet integration: apply gravity, compute candidate position ---
    vec3 gravity = vec3(0.0, -GRAVITY, 0.0);
    p.vel.xyz += gravity * pc.dt;
    vec3 p_new = p.pos.xyz + p.vel.xyz * pc.dt;

    // --- 2. Jacobi structural constraints (4-connected neighbors) ---
    // Rest length: grid spans 4 m world-space across (width-1) cells.
    float rest = CLOTH_WORLD_SIZE / float(pc.width - 1u);
    vec3  corr = vec3(0.0);
    float nc   = 0.0;

    // Read neighbor old positions from inP[] to avoid data hazards.
    // For each neighbor: project p_new toward the rest-length sphere around neighbor.
    #define CONSTRAIN(nx, ny) {                                              \
        Particle nb = inP[(ny) * pc.width + (nx)];                           \
        vec3  d     = p_new - nb.pos.xyz;                                    \
        float len   = length(d);                                             \
        if (len > CONSTRAINT_EPSILON) {                                      \
            float wt = p.pos.w / (p.pos.w + nb.pos.w + MASS_EPSILON);      \
            corr -= wt * ((len - rest) / len) * d;                          \
            nc   += 1.0;                                                     \
        }                                                                    \
    }

    if (x > 0u)               CONSTRAIN(x - 1u, y)
    if (x < pc.width  - 1u)   CONSTRAIN(x + 1u, y)
    if (y > 0u)               CONSTRAIN(x,      y - 1u)
    if (y < pc.height - 1u)   CONSTRAIN(x,      y + 1u)

    if (nc > 0.0) p_new += corr / nc;

    // --- 3. Derive velocity from displacement, apply damping ---
    // Clamp speed to prevent explosion when large corrections first reach
    // particles that were in free fall (Jacobi propagation takes ~width frames).
    p.vel.xyz = (p_new - p.pos.xyz) / pc.dt;
    float v_sq = dot(p.vel.xyz, p.vel.xyz);
    if (v_sq > MAX_SPEED * MAX_SPEED) {
        p.vel.xyz *= MAX_SPEED / sqrt(v_sq);
        p_new = p.pos.xyz + p.vel.xyz * pc.dt; // keep position consistent with capped velocity
    }
    p.vel.xyz *= exp(-DAMPING_RATE * pc.dt);
    p.pos.xyz  = p_new;

    outP[idx] = p;
}
