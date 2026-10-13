# Renderer Fidelity Roadmap: From Correct to Expensive-Looking

Notes: I'd like to have both RTGI and Probe-based DDGI
## Motivation

An inventory of the current Vulkan deferred pipeline (Sept 2026) shows the *hard*
infrastructure is already in place: bindless descriptor heap (`VK_EXT_descriptor_heap` +
`VK_KHR_maintenance5`), bindless resource indexing (`VK_EXT_descriptor_indexing`), a
mesh/task-shader meshlet geometry path (`VK_EXT_mesh_shader`, `drawIndirectCount` wired
for `vkCmdDrawMeshTasksIndirectCountEXT`), and a full ray-tracing pipeline
(`VK_KHR_acceleration_structure` + `VK_KHR_ray_tracing_pipeline`) currently used only by
the path tracer (`renderer_3d_pathtracer.cpp`). Lighting already does tile-based
point-light culling, 4-cascade CSM + point cubemap shadows, SSAO, and a full glTF
metallic-roughness material model with clearcoat/sheen/transmission/iridescence/
anisotropy *parsed* (`material.h:42-94`) but not yet shaded.

What's missing is almost entirely the **polish and scale layer**, not core architecture:
no IBL (ambient is a flat `vec3(0.08)` despite `SkyboxComponent` existing since b61b8e0),
zero post-processing (no bloom/AA/color grading/DoF), no SSR, no GTAO/RTAO, no
volumetrics, no contact shadows, spot lights modeled but unwired, and no GPU-driven
culling despite already owning task shaders and indirect-count draws.

**Target tier:** roughly UE4-era deferred rendering (~4.20-4.25, 2018-2019) / mid-2010s
Frostbite — IBL, TAA, bloom, SSR-or-RT-reflections, GTAO, volumetric fog, PCSS. Not
UE5/Lumen/Nanite (virtualized geometry + fully dynamic software-rasterized GI is a
multi-year team effort and would mean rewriting the geometry pipeline for marginal
payoff at this scene scale). Every phase below builds on the existing frame graph,
descriptor heap, and mesh-shader path rather than replacing them.

This document is the master sequencing plan across all renderer subsystems. Bloom and
IBL already have a detailed, implementation-ready writeup in
[`../deferred_renderer/lighting_improvements.md`](../deferred_renderer/lighting_improvements.md)
(shader snippets, binding layout, precompute pipeline) — **Phase 1 below points there
instead of re-deriving it.** Everything else here (post-process AA, shadow/RT hybrids,
reflections/GI, volumetrics, GPU-driven scale, material completion) is new.

## Invariants (hold across every step)

- **`VulkanContext` stays renderer-agnostic.** Every new resource introduced by this
  roadmap — IBL cubemaps, bloom mip chains, TAA history buffers, GTAO textures, SSR/RT
  reflection targets, volumetric froxel volumes, Hi-Z pyramids, GPU culling buffers —
  is a renderer concept and must live on `VulkanBackend`, `Renderer3D`, or (per the
  `VulkanRendererGlobals` migration) the renderer's own globals object — **never** as a
  `VulkanContext` member. Fitness check: if the resource's name contains a
  renderer-pass concept (bloom, ibl, taa, gtao, ssr, froxel, hiz, cull), it cannot be a
  `VulkanContext` field. Catch it on sight.
- **The frame graph is the only per-frame driver.** Every new pass (IBL precompute is
  the one exception — it's event-driven, not per-frame) registers as a frame-graph
  executor in `assets/frame_graphs/deferred.hnfg` and runs through `frame_graph.cpp`,
  the same as GBuffer/ShadowDraw/SSAODraw/DeferredLighting today. No pass bypasses the
  graph with ad-hoc command buffer submission.
- **`GeometryPath: Meshlet` and mesh/task shaders are the geometry path going forward.**
  `renderer_3d_forward.cpp` / `renderer_3d_geometry.cpp` are legacy/alternate paths —
  new work (GPU culling, LOD) targets the meshlet task-shader path, not the legacy ones,
  unless a phase explicitly says otherwise.
- **No GPU-visible buffer is host-written while the GPU may read it.** Same rule as the
  existing globals system: device-local + per-frame staging + barriers, never
  host-visible memory read directly by shaders. Applies to every new SSBO/UBO this
  roadmap introduces (light probes, froxel data, indirect draw/cull buffers).
- **Every new post-process/lighting feature gets a `config/settings.yaml` toggle and,
  where it has a cost/quality knob, an exposed parameter** (matching the existing
  pattern: `DirShadowDistance`, anisotropy, `EditorCameraExposure`). Nothing should be
  hardcoded on/off — these are exactly the kind of features you'll want to A/B while
  tuning a scene.
- **RT infrastructure is shared, not path-tracer-private.** Phases that add hybrid RT
  (shadows, reflections) reuse the existing acceleration structures built for the path
  tracer rather than standing up a second BLAS/TLAS management path. If the path
  tracer's AS lifecycle doesn't cleanly support a second consumer, that's a prerequisite
  refactor, not a reason to duplicate it.

## Sequencing rationale

Ordered by (visual impact) / (effort), with hard dependencies noted. Phases 1-3 are
independent of each other and can be done in any order among themselves; Phase 4
(reflections) benefits from Phase 1 (IBL) existing as its fallback; Phase 5 shares data
structures with the existing tiled point-light culling; Phase 6 is orthogonal to
everything else (a scale problem, not a fidelity one) and only matters once scene
complexity is actually the bottleneck; Phase 7 is cheap but pairs best *after* Phase 1
because clearcoat/sheen look visibly wrong under flat ambient.

```
Phase 1 (IBL) ──────────────┬─────────────────────► Phase 4 (Reflections/GI)
Phase 2 (Post-process) ─────┤
Phase 3 (Shadow polish) ────┘
Phase 5 (Volumetrics) ──────► (reuses Phase 2's tile-light-grid data)
Phase 6 (GPU-driven scale) ─► independent, do when scene complexity demands it
Phase 7 (Material completion) ─► after Phase 1 (clearcoat/sheen need real ambient)
Phase 8 (Bindless resource mgmt) ─► independent, ergonomics-driven, not fidelity-driven
```

---

## Phase 1 — Image-Based Lighting (Done)
**Effort:** 3-5 days. **Status:** fully scoped already — see
[`lighting_improvements.md § 2`](../deferred_renderer/lighting_improvements.md).

Summary of what that doc specifies (don't duplicate the detail here, just the
sequencing hook): irradiance map (32×32 cubemap, diffuse convolution), prefiltered
specular mip chain (256×256 down to 1×1, GGX importance sampling per mip), BRDF
integration LUT (512×512, bake once and ship as an asset). New bindings at set=1,
bindings 6-8 in `Renderer3D_DeferredLighting.glsl`. Precompute triggers whenever
`SkyboxComponent`'s environment changes, not per-frame.

**Addition to that plan not yet written down:** expose `IblIntensity` in
`config/settings.yaml` alongside the existing `EditorCameraExposure`, and wire an
editor UI control (probably in the same panel that exposes `SkyboxComponent`) so IBL
strength can be tuned live against direct lighting rather than only via a scene-file
default.

**Do this first.** It's the highest visual-impact-per-day item on the whole roadmap,
and the skybox groundwork (b61b8e0, 22c4d6d, b55dd3f) is already committed.

---

## Phase 2 — Post-Process Stack

New frame-graph passes after `DeferredLighting`, before final present. This phase
establishes the post-process chain infrastructure itself (HDR ping-pong targets,
pass ordering) that Phases 4-5 also hook into, so get the plumbing right here even
if you only implement bloom first.

**Fully scoped already** — see
[`post_process_stack.md`](post_process_stack.md) for the file/line-level breakdown
(current-state facts verified against the codebase, the HDR-plumbing prerequisite step,
and a concrete per-item build order). Summary below unchanged; don't re-derive detail
already written there.

### 2a. Bloom
**Effort:** 1-2 days. Already scoped in
[`lighting_improvements.md § 1`](../deferred_renderer/lighting_improvements.md) — dual
Kawase filter, threshold or emissive-channel source, composite pre-tonemap.

### 2b. Anti-aliasing: FXAA now, TAA as the real target
**Effort:** FXAA ~1 day (stopgap). TAA ~1-2 weeks (the real target).

MSAA doesn't compose cleanly with a deferred G-buffer + mesh-shader geometry path (no
engine here does MSAA-deferred well without massive bandwidth cost), so skip it
entirely. Two real options:

- **FXAA** — single post-process pass, no G-buffer or geometry changes, ships in a day.
  Treat as a stopgap, not the destination — it blurs sub-pixel detail and does nothing
  for temporal shimmer on thin geometry (the meshlet path's fine silhouettes will
  shimmer under camera motion without temporal accumulation).
- **TAA** — the actual target. Requires:
  - Per-frame camera jitter (sub-pixel offset applied to the projection matrix, needs
    plumbing through wherever the view-projection UBO is built — this is
    `VulkanRendererGlobals`-owned data per the globals migration, not `VulkanContext`).
  - Motion vectors written into an extra G-buffer channel during the GBuffer pass
    (current-frame vs. previous-frame clip-space position, per pixel). This is the
    biggest G-buffer-format change in this roadmap — budget layout/binding-index churn
    accordingly.
  - History buffer + reprojection + neighborhood clamping (variance clipping is the
    standard approach — cheaper than full YCoCg clipping and good enough at this
    fidelity target).
  - Disocclusion/ghosting handling: reject history on large depth or motion-vector
    deltas, fall back to the current-frame sample.
  - **TAA should land before or alongside SSAO/GTAO noise-heavy passes** —
    `lighting_improvements.md` already notes SSAO noise "becomes invisible with TAA,"
    and the same applies doubly to GTAO and any RT-based passes in Phase 4/5, which are
    typically 1 sample/pixel and rely on temporal accumulation to look clean at all.
    Treat TAA as a soft prerequisite for RT reflections/GI, not purely cosmetic.

### 2c. GTAO (replace/augment SSAO)
**Effort:** 2-3 days incremental over the existing `renderer_3d_ssao.cpp` pass.

Ground-truth ambient occlusion (Jimenez et al., Activision 2016) — horizon-based, better
directional occlusion than SSAO's hemisphere sampling, similar cost. Reuses the existing
SSAO pass's depth/normal inputs and blur pass (`renderer_3d_ssao.cpp/.h`,
`Renderer3D_SSAOBlur.glsl`) — this is a shader-algorithm swap plus a horizon-search
change, not a new pass in the frame graph. Also produces a bent normal as a side output,
which Phase 4's IBL specular occlusion can consume for a further quality bump.

### 2d. Color grading
**Effort:** ~1 day, nearly free once 2a/2b exist. 3D LUT (32³ or 16³) sampled after
tonemap, blended with a strength parameter. Exposed as a per-scene asset reference so
different scenes/moods can swap LUTs without a shader change.

### 2e. Specular anti-aliasing (found via bloom, Sept 2026)
**Effort:** 1-2 days for the stopgap + geometry-pass fix. **Priority: before tuning bloom
further, and independent of TAA (2b) — TAA helps but doesn't replace it.**

**Symptom:** bloom turns the reflective Sponza curtains into a field of flickering sparkles.
Bloom didn't create them; it just amplifies existing sub-pixel specular aliasing. A
Karis-average firefly filter in `Renderer3D_BloomThreshold.glsl` made no visible
difference, which fits: on a normal-mapped curtain the bright pixels are *dense* (whole
clusters, not isolated outliers), so neighbouring taps are equally bright and there's
nothing for the filter to down-weight.

**Root cause (verified against the code):** nothing in the shading path limits how peaky
the specular lobe gets.
- `Renderer3D_DeferredGeometry.glsl:186` clamps roughness to a floor of only `0.04`, and
  `Renderer3D_DeferredLighting.glsl:314` re-applies the same `max(..., 0.04)`.
- `distribution_ggx` (`Renderer3D_DeferredLighting.glsl:152`) with `a = roughness²`
  peaks at `1 / (π·roughness⁴)` when `N·H = 1` — for roughness 0.04 that is ≈ 1.2×10⁵.
  A pixel whose normal-mapped normal lands near the half vector therefore writes an HDR
  value 5 orders of magnitude above its neighbours, and the next pixel (slightly
  different normal) is black. That is temporal/spatial shimmer even before bloom; bloom's
  threshold then passes it straight through.
- Normal maps make it worse: the normal is sampled per pixel with no accounting for the
  sub-pixel normal variation that mip filtering averaged away (the classic Toksvig /
  LEAN problem). No screen-space normal-variance term feeds back into roughness anywhere.

**Fix, in order of cost:**
1. **Stopgap (minutes):** raise the roughness floor (both sites above) to ~0.08-0.1 and
   compare on the curtains. Cuts the D peak by 1-2 orders of magnitude. Free, but bends
   material intent for genuinely mirror-like surfaces, so treat as temporary.
2. **Proper (Kaplanyan/Tokuyoshi-style geometric specular AA):** in the geometry pass,
   estimate the screen-space variance of the shading normal from `dFdx/dFdy` of `N`
   (after normal mapping), clamp it, and widen the roughness:
   `roughness' = sqrt(roughness² + min(2·variance, kappa))` (`kappa` ≈ 0.18). Write the
   widened roughness into `gPBRParams.G` so lighting, IBL prefilter lookup, and any
   later RT-reflection cone angle all see the same value. Expose the variance scale and
   clamp as `config/settings.yaml` knobs per the roadmap invariants.
3. **Normal-map variance (Toksvig):** if step 2 leaves residual shimmer on heavily
   normal-mapped materials, also fold in the length of the mip-filtered normal
   (`1 - |avg_normal|`) as extra variance — needs a roughness-adjustment path when the
   normal map is sampled, not just the derivative term.
4. **Safety net (bloom side, keep regardless):** luminance clamp on the bloom input
   (~20-50, exposed as a setting) in the threshold pass so one bad pixel can never
   dominate the mip chain, whatever the material system does later.

**Verify:** Sponza curtains, bloom strength 0, nudge the camera a pixel at a time — the
glints should stay put and dim rather than popping in and out. Then bloom back on: the
sparkle field should be gone with no change to threshold/strength. Also check that a
mirror-like material (roughness ≈ 0.04 on a flat surface) still looks sharp — geometric
AA should barely touch flat surfaces because their normal variance is ~0.

---

## Phase 3 — Shadow & Direct-Lighting Polish

### 3a. Wire up spot lights
**Effort:** ~1 day. `SpotLightComponent` (`components.h:452`, inner/outer cone angle)
exists but the deferred lighting shader's light loop only samples directional + point
today. Pure gap-fill: add the cone attenuation term and a light-type branch (or unify
into a generic light struct) in `Renderer3D_DeferredLighting.glsl`. Cheapest item in
this entire roadmap for the amount of missing functionality it closes.

### 3b. Contact shadows
**Effort:** 1-2 days. Screen-space ray-march from each fragment toward the dominant
light in depth-buffer space, 8-16 steps. Catches small-scale self-shadowing (thin
props, cloth, hair) that a 4096² CSM cascade can't resolve at distance. Cheap, high
perceptual payoff, no new persistent resources — a good "second thing after bloom" if
you want quick wins before committing to TAA.

### 3c. Soft shadows: PCSS vs. hybrid RT shadows — pick one
**Effort:** PCSS ~2-3 days. RT shadows ~3-5 days (given AS infrastructure exists) to
~1-2 weeks (if the path tracer's AS lifecycle needs generalizing per the invariants
above).

The current CSM filtering is fixed-width PCF (comment in the shadow shader references a
constant penumbra width, not distance-based). Two paths to real soft shadows:

- **PCSS** (Percentage-Closer Soft Shadows) — blocker search + variable-radius PCF
  based on light size and blocker distance. Standard rasterized technique, stays
  entirely inside the existing CSM/cubemap infrastructure
  (`renderer_3d_shadow.h/.cpp`).
- **Hybrid RT shadows** — trace a shadow ray per pixel (or per light, denoised) against
  the existing acceleration structures instead of sampling a shadow map at all.
  Because full RT infrastructure is already live for the path tracer, this may
  genuinely be **less total code** than PCSS: no cascade selection, no seam blending
  between cascades, no cubemap face selection for point lights — just a visibility
  query. The tradeoff is needing a denoiser (even a simple spatiotemporal one) since
  1 ray/pixel is noisy, which is why this pairs well with Phase 2b's TAA landing first.

**Recommendation:** if TAA (2b) is already done or in progress, go straight to hybrid
RT shadows — you get soft, physically-correct penumbrae for free (real light-size
softening, no blocker-search tuning) and it removes an entire subsystem's worth of
cascade/cubemap-edge bugs. If TAA is not yet in place, do PCSS first as a
self-contained improvement and revisit RT shadows once temporal accumulation exists.

---

## Phase 4 — Reflections & a First Pass at GI

### 4a. Reflections: RT reflections over SSR
**Effort:** RT reflections ~1-2 weeks (reusing Phase 3c's AS work if done, or building
it here). SSR ~1 week standalone but becomes throwaway work if RT reflections are added
later.

Per the invariants, prefer reusing the existing acceleration structures over building
screen-space ray marching. **Recommendation: skip SSR, go straight to hybrid RT
reflections.** Rationale specific to this codebase: SSR requires a full screen-space
miss/edge-case fallback chain (off-screen hits, disocclusion, thin-object
false-positives) that a real BVH trace simply doesn't have, and you already pay the
cost of maintaining that BVH for the path tracer. Trace one reflection ray per pixel
(roughness-based cone angle or fixed for mirror-only surfaces first), denoise
temporally (reuses Phase 2b's TAA history infrastructure), fall back to Phase 1's
prefiltered IBL cubemap when a ray doesn't need full RT quality (rough surfaces can
just sample IBL — only do RT for low-roughness/mirror-like materials to bound cost).

If RT reflections turn out to be a bigger lift than expected (AS lifecycle
generalization, see invariants), SSR is a reasonable fallback scope-cut — but budget
for it being partially wasted work once RT reflections land later.

### 4b. GI: probe-based, not full dynamic
**Effort:** 2-4 weeks — the largest single item in this roadmap.

Full Lumen-tier fully-dynamic GI is explicitly out of scope (multi-year team effort).
A tractable middle ground:

- **DDGI-style irradiance volumes** — a sparse 3D grid of probes, each storing
  irradiance (and optionally visibility, for probe-based shadowing) via spherical
  harmonics or an octahedral texture per probe. Probes update by tracing rays from each
  probe position against the existing acceleration structures — this is the one place
  in the roadmap where the path tracer's infrastructure is reused almost directly
  (same ray-scene intersection, different output target).
  - Static/sparse probe placement (baked at scene-save time, or a coarse fixed grid
    around the camera) is a much smaller lift than fully dynamic relocatable probes —
    start there.
  - Probe updates can be amortized across frames (update N probes/frame in a
    round-robin) rather than all probes every frame — keeps cost bounded regardless of
    probe count.
- Sample probes in the deferred lighting pass alongside the Phase 1 IBL term — probes
  handle indoor/occluded bounce light, IBL handles sky-visible surfaces. This is
  functionally the same split Unreal uses (reflection captures + Lightmass/Lumen).

This phase is the correct place to stop chasing "more GI" — probe-based diffuse GI
plus Phase 4a's RT specular reflections gets you most of the *look* of a modern
renderer without the engineering cost of fully dynamic geometry-aware GI.

---

## Phase 5 — Volumetric Fog / Lighting
**Effort:** 1-2 weeks.

Froxel-based volumetric fog (screen-space-aligned 3D froxel grid, e.g. 160×90×64),
scattering/absorption integrated front-to-back, shadow-map (or Phase 3c RT shadow)
sampled per froxel for light shafts. **Notably cheaper here than in a from-scratch
engine**: the froxel grid needs the same light-list-per-cell data structure the
existing tiled point-light culling already builds for the deferred pass — the tile
light-index buffers built for `TILE_SIZE`/`MAX_POINT_LIGHTS 32` culling are most of the
data structure a volumetric integrator needs, just extended into a third (depth-slice)
dimension. Reuse rather than rebuild the light-culling compute pass's output format.

Composite as a translucent layer before Phase 2's tonemap (same ordering concern as
bloom — must be pre-tonemap to stay in HDR-correct compositing).

---

## Phase 6 — GPU-Driven Scale

**Not a fidelity phase — a scale phase.** Only prioritize this once scene complexity
(draw calls, triangle count) is the actual bottleneck, not before. Do not let this
phase compete with Phases 1-5 for priority on visual-quality grounds; it doesn't move
per-pixel quality at all.

### 6a. Meshlet culling in the task shader
**Effort:** 3-5 days. The task shader stage exists (`VK_EXT_mesh_shader`,
`renderer_3d_meshlet.cpp`) but currently only toggles cull-mode per material
(double-sided). Add per-meshlet frustum culling and normal-cone backface culling in the
task shader — this is the standard "meshlet task shader does the cheap reject" pattern
and is a natural extension of infrastructure that's already 80% there.

### 6b. Hi-Z occlusion culling
**Effort:** 1 week. Build a hierarchical-Z pyramid from the previous frame's depth
buffer (compute downsample chain, min-reduction), test meshlet bounding volumes against
it in the task shader (or a separate compute cull pass feeding indirect draw counts).
Depends on 6a's per-meshlet bounds already being computed.

### 6c. GPU-driven indirect draw / scene traversal
**Effort:** 1-2 weeks. `drawIndirectCount` is already enabled
(`vk_backend.cpp:1838`, "required for vkCmdDrawMeshTasksIndirectCountEXT") but nothing
currently generates its indirect/count buffers from a GPU cull pass — worth confirming
during 6a/6b scoping whether the current meshlet dispatch already goes through
`vkCmdDrawMeshTasksIndirectCountEXT` or a direct (non-indirect) call; if the latter,
switching over is a prerequisite for 6a-6c to actually pay off.

### 6d. LOD system
**Effort:** 2-3 weeks, largest item in Phase 6. Meshlet-granularity LOD (cluster
hierarchy, select LOD per meshlet-group based on screen-space error) rather than
whole-mesh LOD swapping — fits the existing meshlet pipeline better and avoids popping
artifacts at LOD transitions that whole-mesh swapping has.

---

## Phase 7 — Cash In the Material System
**Effort:** 1-2 weeks total, but cleanly splittable per extension (each is
independently committable, unlike most other phases).

`material.h` already parses clearcoat (`:42`), sheen (`:50`), transmission (`:64`),
iridescence (`:87`), and anisotropy (`:94`) from glTF, but none reach the shader —
confirmed zero references to any of these terms across `assets/shaders/*.glsl`. The
data model is already ahead of the shading model; this phase is pure shader work, no
new C++ pipeline:

- **Clearcoat** — second specular lobe, its own roughness/normal, Fresnel-weighted
  blend over the base layer. Do this one first — most visually obvious (car paint,
  varnished wood, lacquer) and self-contained.
- **Sheen** — Charlie distribution, additive fabric-edge highlight. Cheap once
  clearcoat's "extra lobe" plumbing exists.
- **Anisotropy** — tangent-space-rotated specular lobe (brushed metal, hair). Needs
  correct tangent data in the G-buffer if not already present — check this before
  scoping, it may be the actual blocker rather than the shading math.
- **Transmission** — the expensive one: needs a screen-space refraction pass or a
  color-behind-object sample (a "grab pass" of the pre-transmission scene color),
  since it's not expressible as a local BRDF term alone. Budget this as its own
  sub-effort, not a quick add like the other three.
- **Iridescence** — thin-film interference term (Belcour/Barla model is the standard
  reference), purely a BRDF-math addition once the others establish the multi-lobe
  shading pattern.

**Do this after Phase 1 (IBL)**, not before — clearcoat and sheen both read specular
IBL for their secondary lobe, and under the current flat `vec3(0.08)` ambient they'll
look visibly wrong (flat, chalky) regardless of how correct the BRDF math is. Wiring
these up against flat ambient means redoing the visual validation once IBL lands
anyway.

---

## Phase 8 — Bindless-Everywhere GPU Resource Management
**Not a fidelity phase — a developer-ergonomics phase.** Motivated by how unpleasant classical
Vulkan descriptor management feels compared to writing a normal CPU program (raised while
scoping Phase 1/IBL: reusing the existing bindless *sampled*-image table for irradiance/
prefiltered/BRDF-LUT textures was easy and required no new infra, but the one-shot compute
passes that *write* those textures still need hand-built `VkDescriptorSetLayout` /
`vkAllocateDescriptorSets` / `VkWriteDescriptorSet` boilerplate, same as everywhere else in the
renderer). Goal: extend the descriptor-heap philosophy that already covers reads to cover writes
too, so allocating and touching a GPU resource feels closer to `new`/holding a handle than to
hand-wiring a descriptor set every time.

**Current state (verified, not assumed):**
- `VulkanDescriptorHeap::write_bindless()` hardcodes `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE`
  (`vk_descriptor_heap.cpp:282-284`) — there is no bindless table for storage images
  (compute/write targets) anywhere in the engine today.
- The path tracer (`renderer_3d_pathtracer.cpp:432-2156`) — the most compute-heavy subsystem in
  the renderer — still hand-builds descriptor set layouts/pools/writes for every dispatch. If
  bindless-for-writes already existed anywhere here, this is where it would be; it isn't, which
  is the clearest evidence this is genuinely unbuilt, not just unused.
- The underlying plumbing already half-supports the extension: the heap tracks a storage-image
  descriptor stride (`m_descriptor_sizes.storage_image`, `vk_descriptor_heap.cpp:43`), and
  `write_image()` (which `write_bindless()` calls into) is already generic over
  `VkDescriptorType` — a bindless storage-image table is a real, scoped extension of existing
  code, not a rewrite.

**Shape of the work:**
- Add `alloc_bindless_storage_index()` / `write_bindless_storage()` to `VulkanDescriptorHeap`,
  mirroring the existing sampled-image bindless table but backed by `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`.
- Add the shader-side bindless storage arrays (`layout(set=0, binding=N) uniform image2D
  u_StorageImages[]`, plus a cube variant) to `global_bindings.h`/the generated GLSL include,
  parallel to `u_TextureCube[]`/`u_Textures[]`.
- Migrate passes one at a time rather than in one sweep — the path tracer is the highest-value
  first target (most descriptor churn to eliminate), followed by SSAO, shadows, and the meshlet
  path. Each migration is independently valuable and revertible; don't block on migrating
  everything before any of it ships.
- Buffers (SSBOs/UBOs) are a separate, likely-later extension of the same idea — start with
  images since that's the immediate itch (compute output targets), and only generalize to
  bindless buffer handles if the ergonomic win still feels worth it once images are done.

**Explicitly out of scope for this phase:** this does not remove descriptor sets from Vulkan —
it collapses them to one or two persistent sets built once, with dynamism moved into plain
integers passed around like any other data (same trick the existing sampled-image bindless path
already plays). Framing it as "no more descriptors" would be a false promise; framing it as
"touch the descriptor system once at startup instead of once per draw/dispatch" is the accurate
goal.

**Effort:** 1-2 weeks for the heap extension + first migration (path tracer); each subsequent
pass migration is incremental and separately schedulable. **Do not entangle this with Phase 1
(IBL)** — Phase 1's one-shot bake passes can and should ship with plain hand-written descriptors
first (see `../deferred_renderer/lighting_improvements.md` and the IBL implementation tutorial);
retrofit them onto the bindless-storage path later if desired, once this phase exists, rather
than blocking IBL on infra that doesn't exist yet.

---

## Effort Summary

| Phase | Item | Effort | Depends on |
|---|---|---|---|
| 1 | IBL | 3-5 days | — (see `lighting_improvements.md`) |
| 2a | Bloom | 1-2 days | — |
| 2b | FXAA (stopgap) | ~1 day | — |
| 2b | TAA (target) | 1-2 weeks | — |
| 2c | GTAO | 2-3 days | — |
| 2d | Color grading | ~1 day | 2a/2b pipeline plumbing |
| 2e | Specular anti-aliasing | 1-2 days | — (fixes bloom sparkles; TAA complements it) |
| 3a | Spot lights | ~1 day | — |
| 3b | Contact shadows | 1-2 days | — |
| 3c | PCSS or RT shadows | 2-3 days / 3-5+ days | RT path benefits from 2b (TAA) |
| 4a | RT reflections (preferred) or SSR | 1-2 weeks / ~1 week | Phase 1 (IBL fallback), 3c's AS reuse |
| 4b | Probe-based GI | 2-4 weeks | Phase 4a's RT tracing infra |
| 5 | Volumetric fog | 1-2 weeks | Phase 2's tile-light-grid reuse |
| 6a-c | GPU-driven culling | 3-5 days + 1 week + 1-2 weeks | — (do only when scale-bound) |
| 6d | LOD system | 2-3 weeks | 6a's meshlet bounds |
| 7 | Material completion | 1-2 weeks (splittable) | Phase 1 (IBL) |
| 8 | Bindless-everywhere resource mgmt | 1-2 weeks + per-pass migration | — (do only when the ergonomics pain outweighs the yak-shave; not a fidelity item) |

**If you only do three things:** Phase 1 (IBL) → Phase 2a+2b (bloom + TAA) → Phase 3c
or 4a (RT shadows or RT reflections, leaning into infrastructure you already own
instead of building screen-space fallback logic from scratch).