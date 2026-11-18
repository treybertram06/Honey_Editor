#type compute
#version 450
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// ---------------------------------------------------------------------------
// Particle layout (shared with ClothSeed and ClothRender):
//   pos.xyz  = current world position
//   pos.w    = inverse mass  (0 = pinned)
//   vel.xyz  = PREVIOUS world position  (Verlet state, not a velocity vector)
//   vel.w    = unused
// ---------------------------------------------------------------------------

const float GRAVITY            = 9.81;
const float CLOTH_WORLD_SIZE   = 4.0;  // must match ClothSeed
const float DAMPING_RATE       = 0.5;  // velocity loss per second
const float MAX_SPEED          = 30.0; // m/s cap on implicit velocity
const float CONSTRAINT_EPSILON = 1e-6;
const float MASS_EPSILON       = 1e-8;

// Per-pass stiffness multipliers.
// Shear and bend corrections are applied after structural so they refine
// rather than fight it. Lower multipliers prevent over-constraint.
const float SHEAR_STIFFNESS = 0.8;
const float BEND_STIFFNESS  = 0.3;

struct Particle {
    vec4 pos; // xyz = current pos, w = invMass
    vec4 vel; // xyz = previous pos (Verlet), w = unused
};

layout(set = 0, binding = 0) readonly  buffer InState  { Particle inP[];  };
layout(set = 0, binding = 1) writeonly buffer OutState { Particle outP[]; };

layout(push_constant) uniform SimPC {
    float dt;
    uint  width;
    uint  height;
    uint  phase; // 0 = update (x+y)%2==0 ("black"), 1 = update (x+y)%2==1 ("red")
} pc;

void main() {
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;
    if (x >= pc.width || y >= pc.height) return;
    uint idx = y * pc.width + x;

    // Red-Black Gauss-Seidel: only simulate particles matching the current phase.
    // The other half are written through unchanged so outP[] is always complete.
    // Structural neighbors always have the opposite parity, so phase-0 particles
    // read only phase-1 (stale) data and vice-versa — no data hazard.
    // Phase 1 therefore sees the updated phase-0 positions: true GS ordering.
    if ((x + y) % 2u != pc.phase) {
        outP[idx] = inP[idx];
        return;
    }

    Particle p     = inP[idx];
    float inv_mass = p.pos.w;

    if (inv_mass <= 0.0) { outP[idx] = p; return; }
    if (pc.dt    <= 0.0) { outP[idx] = p; return; }

    vec3 curr = p.pos.xyz;
    vec3 prev = p.vel.xyz; // previous position (Verlet state)

    // --- 1. Verlet prediction ---
    vec3  disp     = (curr - prev) * exp(-DAMPING_RATE * pc.dt);
    float max_disp = MAX_SPEED * pc.dt;
    float disp_len = length(disp);
    if (disp_len > max_disp && disp_len > 0.0)
        disp *= max_disp / disp_len;

    vec3 pred = curr + disp + vec3(0.0, -GRAVITY, 0.0) * (pc.dt * pc.dt);

    // --- 2. Sequential constraint passes ---
    // Each pass accumulates corrections into its own corr/nc, applies them to
    // pred, then the next pass works from the updated pred. This avoids
    // dilution (all 12 constraint types sharing one nc) and prevents structural
    // corrections from being cancelled by shear/bend corrections in the average.
    //
    // Neighbors always read start-of-substep positions from inP[] (Jacobi).

    float rest       = CLOTH_WORLD_SIZE / float(pc.width - 1u);
    float rest_shear = rest * 1.41421356; // sqrt(2)
    float rest_bend  = rest * 2.0;

    // Macro writes into the local `corr` and `nc` variables, and reads the
    // current `pred` — so it naturally picks up corrections from earlier passes.
    #define CONSTRAIN(nx, ny, r) {                                              \
        Particle nb = inP[(ny) * pc.width + (nx)];                             \
        vec3  d     = pred - nb.pos.xyz;                                       \
        float len   = length(d);                                               \
        if (len > CONSTRAINT_EPSILON) {                                        \
            float wt = inv_mass / (inv_mass + nb.pos.w + MASS_EPSILON);       \
            corr -= wt * ((len - (r)) / len) * d;                             \
            nc   += 1.0;                                                       \
        }                                                                      \
    }

    vec3  corr;
    float nc;

    // Pass A: Structural (4-connected) — full stiffness
    corr = vec3(0.0); nc = 0.0;
    if (x > 0u)               CONSTRAIN(x - 1u, y,      rest)
    if (x < pc.width  - 1u)   CONSTRAIN(x + 1u, y,      rest)
    if (y > 0u)               CONSTRAIN(x,      y - 1u, rest)
    if (y < pc.height - 1u)   CONSTRAIN(x,      y + 1u, rest)
    if (nc > 0.0) pred += corr / nc;

    // Pass B: Shear (diagonal) — applied to structurally-corrected pred
    corr = vec3(0.0); nc = 0.0;
    if (x > 0u          && y > 0u)             CONSTRAIN(x - 1u, y - 1u, rest_shear)
    if (x < pc.width-1u && y > 0u)             CONSTRAIN(x + 1u, y - 1u, rest_shear)
    if (x > 0u          && y < pc.height - 1u) CONSTRAIN(x - 1u, y + 1u, rest_shear)
    if (x < pc.width-1u && y < pc.height - 1u) CONSTRAIN(x + 1u, y + 1u, rest_shear)
    if (nc > 0.0) pred += corr / nc * SHEAR_STIFFNESS;

    // Pass C: Bend (skip-1) — soft smoothing applied last
    corr = vec3(0.0); nc = 0.0;
    if (x >= 2u)             CONSTRAIN(x - 2u, y,      rest_bend)
    if (x + 2u < pc.width)   CONSTRAIN(x + 2u, y,      rest_bend)
    if (y >= 2u)             CONSTRAIN(x,      y - 2u, rest_bend)
    if (y + 2u < pc.height)  CONSTRAIN(x,      y + 2u, rest_bend)
    if (nc > 0.0) pred += corr / nc * BEND_STIFFNESS;

    // --- 3. Advance Verlet state ---
    p.vel.xyz = curr; // new prev_pos = old curr_pos
    p.vel.w   = 0.0;
    p.pos.xyz = pred;
    // p.pos.w (inv_mass) unchanged

    outP[idx] = p;
}
