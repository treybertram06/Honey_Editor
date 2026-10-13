# Phase 2b — Anti-Aliasing (FXAA → TAA): Implementation Guide

This is the detailed version of `post_process_stack.md` Steps 2 (FXAA) and 3 (TAA). It's written
as a guide for you to implement yourself: it explains what to build, where it goes, and why. It
does not include finished code. It was checked against the codebase as of 2026-09-27, after
Step 0 (HDR plumbing) and Step 1 (bloom, commits `ecb77d9`/`34ebd14`/`951bfb4`) had landed.

Part A (FXAA) is about a day of work and doesn't depend on Part B. Part B (TAA) is the real
goal. It has four parts that build on each other: a frame-graph feature, a G-buffer format
change, camera jitter, and the resolve shader. Do them in the order written.

---

## Inherited invariants — do not violate

These come from `CLAUDE.md` → Architecture Invariants, `roadmap.md`, and `post_process_stack.md`.
The comment after each one says how this plan is likely to break it.

- **`VulkanContext` stays renderer-agnostic.** The TAA history textures, the velocity
  attachment, jitter state, previous-frame matrices, and FXAA/TAA parameter buffers all belong
  to the renderer. They live in the frame graph, `Renderer3D` / a `Renderer3DTAA` module, or
  `VulkanRendererGlobals`. They never go on `VulkanContext`.
  *Smell test:* a field named `taa*`, `history*`, `velocity*`, `jitter*`, or `fxaa*` on
  `VulkanContext` is a bug.
  *Where this plan is at risk:* TAA history must survive across frames. "Persistent across
  frames" can look like device-lifetime state that belongs on the context. It doesn't. See
  B1 for where it actually goes.
- **The frame graph is the only per-frame driver.** FXAA, TAA resolve, and the history
  handling are all `deferred.hnfg` passes with registered executors. Don't do a side-channel
  `vkCmdCopyImage` of history outside the graph.
  *At risk in B1:* the easy way to keep history alive is to hand-manage an image outside the
  graph. Don't.
- **No GPU-visible buffer is host-written while the GPU may read it.** This applies to
  `fxaaParams` and `taaParams`. Follow the `bloomParams` pattern exactly
  (`renderer_3d_bloom.cpp:101-119`): a frame-graph `Buffer`, filled via
  `ctx.get_buffer(...)->set_data(...)`, and only re-uploaded when the settings or buffer
  identity change. `taaParams` is special: it changes **every frame** because the jitter and
  the reset flag change. Check that `set_data` on a frame-graph buffer is per-frame-slot safe
  before relying on it. If it isn't, the per-frame values go in the push constant or the
  `CameraUBO` (see B3), not in a buffer the GPU may still be reading.
- **Pass-local uniform data is a frame-graph `Buffer` resource, never a `global_bindings.h`
  entry.** The one *deliberate* exception in this plan is B3: `prev_view_proj` and the
  jitter go into `CameraUBO`. They are real per-view, per-frame camera state that the G-buffer
  pass, TAA, and later SSR/RT denoisers all read (Unreal puts `PrevViewProj` in its View
  uniform buffer for the same reason). Everything that is TAA's *own* tuning (feedback,
  clip gamma, sharpness) goes in `taaParams`.
- **Every feature gets a `config/settings.yaml` toggle and exposed parameters.** Follow the
  nested-struct pattern bloom introduced (`settings.h:21-27`, `RendererSettings::BloomSettings`),
  the load/save pair in `settings.cpp`, and a UI block in `renderer_debug_panel.cpp:325-331`.
- **`AttachmentBlendState` is a bool. There is no hardware blend-factor support**
  (`pipeline_spec.h:38-44`). Any blend (TAA history mix, icon overlay) happens in the shader.
- **`MatchSize` doesn't chain.** Every new texture below uses `MatchSize: gAlbedo`.
- **`GeometryPath: Meshlet` is the geometry path going forward.** Velocity output is
  first-class on the meshlet G-buffer shader. The classic path gets the camera-only fallback
  (B2e) and nothing more.

### New invariants this plan adds (later sub-plans such as SSR/RT denoisers inherit them)

- **Jittered vs. unjittered is decided by the consumer, never ad hoc.** Anything that
  *rasterizes into* the G-buffer or *reconstructs position from* its depth uses the
  **jittered** matrices (G-buffer draw, deferred lighting's `u_InvViewProjection`, SSAO).
  Everything else uses **unjittered** matrices: shadow cascade fitting, culling, editor
  overlays composited after AA, and motion-vector math.
  *Smell test:* if CSM cascades shimmer while the camera is still and TAA is on, jitter leaked
  into shadow fitting.
- **Velocity is jitter-free and in UV units.** `gVelocity.rg = uv_current - uv_previous`,
  computed from *unjittered* clip positions. Anything that reads velocity (TAA now, SSR and
  denoisers later) can rely on this definition.
- **History is per-view state and is explicitly invalidated.** Resize, frame-graph rebuild,
  renderer-type switch, AA-mode switch, and camera cuts all set "history invalid". The
  resolve then outputs the current frame alone. Never let uninitialized history reach the
  output.

---

## Ground truth this plan depends on (verified, not assumed)

- **The current composite pass does three jobs.** `Renderer3D_PostProcessComposite.glsl`
  applies bloom, exposure, and ACES tonemap + gamma (`:62-67`). It also runs the vector-icon
  overlay and resolves entity-id picking (`:69-78`). It writes `editorViewport` directly
  (`deferred.hnfg:347-358`). FXAA has to run *between* the tonemap and the overlay, so this
  pass gets split (A1).
- **The pipeline's attachment count is hardcoded per module.** `perColorAttachmentBlend.resize(N)`
  sets it: 2 for composite (`renderer_3d_postprocess.cpp:32`), 1 for bloom, and **4 for the
  meshlet G-buffer** (`renderer_3d_meshlet.cpp:43`, with a comment naming the 4 attachments).
  Adding a G-buffer attachment means updating that count and every other pipeline that draws
  into the G-buffer render pass. Vulkan requires the pipeline's `colorAttachmentCount` to match
  the subpass exactly.
- **The G-buffer is an external framebuffer with 5 name aliases.** It is built at
  `scene_viewport_renderer.cpp:121-132` (`{RGBA8, RGBA16F, RGBA8, RED_INTEGER, Depth}`) and
  registered under `gAlbedo..gDepth` at `:291-295`. The frame graph gets each alias's
  `attachment_index` from its position in `GBuffer`'s `Writes:` list (`deferred.hnfg:189-192`,
  plus the comment at the top of the file). A new attachment has to be inserted consistently
  in **both** lists.
- **There is no 2-channel format.** `FramebufferTextureFormat` (`framebuffer.h:8-22`) is
  `RGBA8, RGBA16F, RED_INTEGER, DEPTH24STENCIL8, D32_SFLOAT`. The Vulkan mapping is in
  `vk_framebuffer.cpp:82-92` and the YAML parser in `frame_graph_loader.cpp:259-270`. This
  answers the open question `post_process_stack.md` 3a left for later.
- **The frame graph has no notion of "previous frame."** `frame_graph.cpp:1402-1470` builds
  one producer per resource and adds a producer→reader edge for every read. A pass that
  reads last frame's `X` while a later pass writes this frame's `X` is reported as a **cycle**.
  A `Texture` read with no producer is a hard error (`:1453-1456`). This is why B1 exists.
- **Descriptors are re-resolved every frame; framebuffers are not.** `bind_heap_pipeline`
  caches a *plan* that maps shader bindings to resource handles (`frame_graph.cpp:870-968`).
  It writes the actual image views into the transient heap block **every frame** (from
  `:985`). So swapping which physical image backs a resource between frames works for
  *reads*. A pass's *target framebuffer* is fixed at compile time (`pass.target_framebuffer`,
  `:1336-1340`), so swapping for *writes* needs a small compile-time change (B1).
- **Camera matrices are GL-style until the last moment.** `EditorCamera` builds a GL
  projection (`editor_camera.cpp:119-131`). Vulkan clip correction (Y flip, Z remap) happens
  only in `make_gpu_camera` (`vk_renderer_globals.cpp:11-26`), which also computes
  `inv_view_proj`. So a jitter applied to the GL projection comes out with **Y negated** in
  Vulkan clip space. See B3c.
- **Camera state is copied into `Renderer3DData` by `begin_scene`**
  (`renderer_3d.cpp:107-133`, called from `scene.cpp:1158`). Then **every** executor
  rebuilds and re-submits the `CameraUBO` from those copies (`renderer_3d_deferred.cpp:72-77`,
  `renderer_3d_ssao.cpp:149-152`, `renderer_3d_postprocess.cpp:67-73`, and shadows at
  `renderer_3d_shadow.cpp:476-478`). Jitter has to be applied at one source, and each consumer
  has to pick jittered or unjittered deliberately.
- **`CameraUBO` is declared in 9 shaders.** `grep -rln "uniform CameraUBO" assets/shaders`
  returns 9. The C++ struct is `gpu_types.h:56-64`. Adding a field without a shared include
  means 9 manual edits that can drift apart (B3a).
- **Per-draw transforms reach the meshlet shader through `GPUDrawData`**
  (`gpu_types.h:111-117`, mirrored in `Renderer3D_MeshletDeferred.glsl:13-19`). They are
  rebuilt from scratch every frame from `meshlet_draws` (`renderer_3d_meshlet.cpp:190-200`).
  Nothing keeps a previous transform today.
- **Editor overlays are drawn *inside* the G-buffer pass.** `execute_scene_pass` calls
  `post_scene_overlay_render` (`scene_viewport_renderer.cpp:330-335` → `editor_layer.cpp:612`,
  Renderer2D grid, colliders, and so on). They get jittered and TAA'd along with scene
  geometry, and they write no velocity. B2e covers them; the long-term fix is noted at the
  end.
- **Bloom's firefly problem is relevant here.** Your last three commits fought bloom sparkles.
  TAA placed **before** bloom (B4a) feeds bloom a temporally stable image, and that is
  probably the most effective firefly fix you have available. `roadmap.md` 2e (specular AA)
  is still the root-cause fix. TAA helps but doesn't replace it.
- **A settings bug to fix while you're in there.** `settings.cpp:217-222` loads
  `BloomThreshold` and `BloomSoftKnee` into `bloom.strength`, so both values are lost on
  every load. You'll be editing this file for the AA settings anyway.

---

# Part A — FXAA

## Concept (short)

FXAA (Lottes, NVIDIA 2009–2011, "FXAA 3.11") works on one image, after tonemapping. For each
pixel it:

1. Measures local luma contrast (the pixel plus its 4 neighbors). If the contrast is below a
   threshold, the pixel is not on an edge and it exits early. That's most pixels.
2. Classifies the edge as horizontal or vertical (weighted second derivatives over the 3×3),
   and decides which side of the pixel the edge lies on.
3. Walks along the edge in both directions until the luma "leaves" the edge, which gives the
   distance to each end of the edge.
4. Turns "how far am I from the nearer end" into a sub-pixel offset *perpendicular* to the
   edge, and re-samples the image **bilinearly** at that offset. Blending comes from the
   texture filtering hardware, not from explicit neighbor weights.
5. Separately, computes a *sub-pixel aliasing* term (average neighborhood luma vs. center)
   for single-pixel features. It uses the larger of the two offsets.

**Why post-tonemap and gamma space:** the thresholds are tuned for perceptual [0,1] luma. In
linear HDR, a highlight of 50.0 next to 0.2 counts as "infinite contrast," and dark-region
edges fall under the threshold.

**What FXAA can't do:** it only sees one frame and one sample per pixel. It can't recover
detail smaller than a pixel, so thin meshlet silhouettes still crawl as the camera moves. It
also blurs texture detail a little. That's why it's the stopgap and TAA is the target.

## A1. Split the composite pass: tonemap → `ldrColor`, output pass → `editorViewport`

**What:** `PostProcessComposite` currently writes `editorViewport` and also does the overlay
and picking. Split it:

- `PostProcessComposite` keeps bloom, exposure, tonemap, and gamma. It writes a new transient
  `ldrColor` (`Format: RGBA8`, `MatchSize: gAlbedo`). **Put luma in `.a`**: compute it from the
  gamma-encoded color, e.g. `dot(rgb, vec3(0.299, 0.587, 0.114))`. FXAA 3.11 expects luma in
  alpha. Computing it once here saves recomputing it for all ~9 taps FXAA reads per pixel.
  Its attachment count drops from 2 to 1.
- New pass `PostProcessOutput` (executor `postprocess.output`). It reads `ldrColor`,
  `hdrEntityId`, `vectorTexture`, `vectorEntityTexture`, and `fxaaParams`. It writes
  `[editorViewport]` with 2 color attachments (color + entity id). Its shader runs
  FXAA-or-copy on `ldrColor`, then does the vector-icon `mix` and the picking resolve that
  currently sit in composite (`:69-78`), unchanged.

**Why this order:** icons are editor UI. They should be crisp and not anti-aliased, and TAA
later *must not* touch them (they don't jitter and have no velocity). Putting the overlay in
the very last pass, after all AA, sets that up once so it holds for both AA modes.

**Design choice to own:** you could make FXAA its own pass that writes a second LDR target,
with a separate overlay pass after it. That's cleaner in isolation, but it costs one more
full-screen RGBA8 write/read for no benefit. FXAA reads its taps and writes one value per
pixel, so fusing it into the final pass is the usual choice. Put the FXAA algorithm itself in
`assets/shaders/postprocessing/fxaa.glsli` so the output shader just calls
`vec3 fxaa(texture2D, sampler, vec2 uv, vec2 rcp_frame, FxaaParams p)`. That keeps it reusable
(the forward graph may want it later) and keeps the output shader readable.

**Where:**
- `deferred.hnfg`: new `ldrColor` resource, retarget composite's `Writes:`, remove the icon
  and entity-id `ReadBindings` from composite, add the new pass after it.
- `renderer_3d_postprocess.cpp`: add a second pipeline cache map, a second `execute_*`, and a
  second `register_executor`. Structure them exactly like the existing composite (`:22-37`,
  `:59-92`). The new pipeline uses `perColorAttachmentBlend.resize(2)`; composite's becomes 1.

**Verify before writing any FXAA code:** with the output shader as a pure copy + overlay, the
image and picking must be **identical** to before. One detail can cause off-by-one errors:
entity id should keep using `texelFetch` with `gl_FragCoord`, as composite does now at `:70`.
Every pass here uses the same `MatchSize: gAlbedo` resolution, so that stays valid.

## A2. Settings: `AntiAliasingSettings`

**What:** follow bloom's nested-struct pattern (`settings.h:21-27`):

```
AntiAliasingSettings
  mode          : enum class AntiAliasingMode { none, fxaa, taa }   (taa unused until Part B)
  fxaa          : { subpix, edge_threshold, edge_threshold_min }    + operator== = default
  taa           : { ... }                                            (filled in B6)
```

Defaults are Lottes' "default quality" values: `subpix 0.75`, `edge_threshold 0.166`,
`edge_threshold_min 0.0833`. Also provide the high-quality values (`0.125` / `0.0625`) as
suggested ranges in the UI tooltip.

**Serialization:** mode is a string (`"None" | "FXAA" | "TAA"`), following the
`geometry_path_to_string` / `string_to_geometry_path` pair (`settings.cpp:94-110`), not an
int. The UI is an `ImGui::Combo`, following `renderer_debug_panel.cpp:224-229`, with the FXAA
sliders shown only when `mode == fxaa`.

**Toggling:** don't rebuild the graph when the mode changes. The pass stays, and `mode` goes
into `fxaaParams` as an int. The shader branches on it, and the branch is uniform across the
draw, so it costs almost nothing. This is the same "parameter, not graph surgery" rule the
parent doc set for `BloomEnabled`.

## A3. `fxaaParams` buffer

**What:** a `Type: Buffer` resource (16 bytes: `subpix`, `edge_threshold`,
`edge_threshold_min`, `int enabled`). Bind it as `{ Usage: uniform, Shader: FxaaParamsUBO }`.
Upload with the same `weak_ptr` + `operator==` cache pattern as `bloomParams`
(`renderer_3d_bloom.cpp:101-119`). Add a `FxaaParamsUBOData` next to `BloomParamsUBOData` in
`gpu_types.h`, with a `static_assert` on its size, as the other GPU structs have.

**Not in the UBO:** `rcp_frame` (1/width, 1/height). Get it in the shader from
`1.0 / vec2(textureSize(sampler2D(u_LdrColor, ...), 0))`. It always matches the texture being
sampled, including after a resize, with nothing to keep in sync.

## A4. The FXAA shader (`fxaa.glsli`)

This is the substantive part. Implement it from Lottes' FXAA 3.11 description. Don't paste a
copy. The tricky parts, in the order you'll run into them:

1. **Sampler: linear filtering + clamp-to-edge is required.** FXAA's final step *depends* on
   bilinear filtering, since the blend comes from sampling between two texels. The walk
   samples up to ~12 steps along the edge and will read past the image border. Commit
   `34ebd14` was this exact bug in bloom (edge taps wrapping to the other side of the screen),
   so reuse the clamp-to-edge linear sampler that fix settled on.
2. **Early exit:** `range = max_luma - min_luma` over the center and N/S/E/W taps. Exit if
   `range < max(edge_threshold_min, max_luma * edge_threshold)`. The *relative* threshold
   stops bright regions from triggering on noise. The *absolute* minimum stops dark regions
   from triggering on nothing. You need both.
3. **Orientation:** use weighted second derivatives over the full 3×3, i.e. corners too.
   A horizontal edge has a large vertical gradient: compare
   `|N + S - 2C|·2 + |NE + SE - 2E| + |NW + SW - 2W|` against its transpose.
4. **Choose the side:** compare the gradients toward the two perpendicular neighbors. Step
   half a texel toward the steeper one. That's the "edge center" line the walk follows.
5. **The walk:** move along the edge in both directions using the quality preset's step
   table (FXAA 3.11 preset 12 is roughly `1, 1, 1, 1, 1.5, 2, 2, 2, 2, 4, 8`). At each step,
   sample luma on the edge line and stop when `|luma_end - luma_edge_avg| >= gradient * 0.25`.
   **Don't unroll the loop by hand**: a fixed-count `for` loop with an early `break` is what
   the compiler handles best.
6. **Edge offset:** take the nearer end. If the luma at that end is on the "wrong side" (its
   sign relative to the center doesn't match), the pixel isn't where the edge starts, and the
   offset is 0. Otherwise `offset = 0.5 - dist_nearest / (dist_n + dist_p)`.
7. **Subpixel term:** `subpix = |avg(3×3 weighted) - center| / range`, clamped, then
   `smoothstep`-shaped and squared, times `subpix_quality`. Final offset is
   `max(edge_offset, subpix_offset)`.
8. **Final sample:** `uv + offset * rcp_frame * perpendicular_dir`, bilinear.

**Things that look fine in testing and are wrong:**
- Using `texelFetch` for the taps because it's "exact": it breaks step 8, which relies on
  filtering.
- Computing luma from linear RGB: wrong space, and edges in dark regions disappear.
- Running FXAA on `hdrColor`: see "Why post-tonemap" above.

**Debug view:** add `debug_view` to `fxaaParams` (0 = off, 1 = edge mask (red where FXAA ran),
2 = horizontal/vertical classification). You'll need it to tune thresholds, and it's the
first thing to check if something looks wrong.

---

# Part B — TAA

## Concept

Rasterization takes one sample per pixel, at the pixel center. TAA gets *many* samples per
pixel **over time**. Each frame it offsets the projection by a different sub-pixel amount
(jitter). The resolve pass keeps a running average (the *history*) of what each surface
point looked like. The camera and objects move, so "the same surface point" is at a
different pixel each frame. **Motion vectors** say where each pixel was last frame
(reprojection). History can be wrong (disocclusion, lighting changes, bad motion vectors), so
before blending, history is constrained to the plausible color range of the *current*
frame's 3×3 neighborhood. This is **neighborhood clipping**, and it's what separates
production TAA from a ghosting mess.

The standard references, in order of usefulness for this implementation:
- Karis, *"High Quality Temporal Supersampling"*, SIGGRAPH 2014 (UE4 TAA): the overall
  structure, YCoCg, closest-depth velocity, luma weighting.
- Salvi, *"An Excursion in Temporal Supersampling"*, GDC 2016: **variance clipping**, the
  clipping method recommended here.
- Pedersen, *"Temporal Reprojection Anti-Aliasing in INSIDE"*, GDC 2016: a readable,
  complete resolve including clip-toward-center.
- Jimenez, *"Filmic SMAA"*, SIGGRAPH 2016: the optimized 5-tap Catmull-Rom history fetch.

## B0. Order of work within Part B

Each step can be tested on its own, and the order limits risk:

1. **B1 — Frame-graph history resources.** Infrastructure only, no visual change.
2. **B2 — Velocity G-buffer attachment.** Verify with a debug view. Still no AA.
3. **B3 — Camera jitter + `CameraUBO` extension.** Verify the image wobbles by under half a
   pixel, and that shadows don't shimmer.
4. **B4 — Resolve pass.** This is where AA actually happens.
5. **B5 — Invalidation, sharpening, polish.**
6. **B6 — Settings/UI.**

Commit after each step. B2 and B3 are the steps most likely to cause regressions.

## B1. First-class history resources in the frame graph

**The problem:** the resolve reads *last* frame's result and writes *this* frame's. With the
current compiler (`frame_graph.cpp:1402-1470`) that is either a cycle (the same resource is
read by the resolve and written later) or a "texture read before any pass writes it" error.

**Three options:**

| Option | How | Verdict |
|---|---|---|
| Hand-managed image outside the graph | `Renderer3DTAA` owns a `VkImage`, copies into it with raw commands | **Reject.** Breaks the "frame graph is the only per-frame driver" invariant and duplicates the graph's barrier and layout tracking. |
| External-framebuffer alias hack | Register one externally owned framebuffer under two names (`taaHistoryPrev` with no in-graph producer, `taaHistory` written by a copy pass), the same trick as the `gAlbedo..gDepth` aliases | **Bootstrap only.** It works with zero compiler changes, but it hides a cross-frame hazard from the graph, needs an extra copy pass, and every future temporal effect (SSR, RT denoise, GTAO accumulation) would repeat the hack. If you use it to get TAA on screen sooner, leave a `TODO(history)` and do option 3 before a second temporal consumer shows up. |
| **Graph-owned double-buffered resources** | A resource flag makes the graph allocate 2 physical textures and swap them every frame. A read marked "previous frame" resolves to the other one and adds no dependency edge. | **Recommended.** This is how UE's RDG (`FRDGBuilder::QueueTextureExtraction` + view-state history) and Frostbite's FrameGraph handle it. It's ~1–2 days, and every later temporal feature gets it for free. |

**Shape of option 3:**

- **YAML** (`frame_graph_loader.cpp`, the resource parser around `:222-270`): a
  `History: true` key on a `Type: Texture`. A read binding gets `PreviousFrame: true`:
  ```yaml
  taaHistory: { Type: Texture, Format: RGBA16F, MatchSize: gAlbedo, Layers: 1, History: true }
  ...
  ReadBindings:
    - { Name: taaHistory, Usage: sampled, Shader: u_History, PreviousFrame: true }
  Writes: [taaHistory]
  ```
- **Compile (dependency pass, `:1431-1470`):** a `PreviousFrame` read is skipped entirely,
  with no edge and no read-before-write error. Validate that `PreviousFrame` is only allowed
  on a `History` resource, and that a `History` resource has exactly one in-graph writer.
  Otherwise emit a diagnostic. A `History` resource with no writer is a config error.
- **Allocation:** the transient allocator creates 2 framebuffers for a `History` resource
  instead of 1. Store both on the compiled resource (`framebuffers[2]`) with a
  `current_index`.
- **Per-frame swap:** at the top of `FrameGraph::execute()`, flip `current_index` for every
  history resource. Then:
  - *Writes:* the writing pass's `target_framebuffer` is resolved at compile time
    (`:1336-1340`). Give the pass a way to pick `framebuffers[current_index]` at execution
    instead, e.g. store the resource handle and resolve in `get_pass_target_framebuffer()`.
  - *Reads:* the per-frame descriptor write in `bind_heap_pipeline` (from `:985`) resolves the
    image view by handle. Give `PassDescriptorPlanEntry` a `previous_frame` bool so the
    per-frame write picks `framebuffers[current_index ^ 1]` for it and
    `framebuffers[current_index]` for normal reads.
- **Validity:** the graph knows when history is garbage, because it just allocated it. Keep a
  `frames_since_alloc` counter per history resource, set to 0 on (re)allocation, and expose
  `ctx.is_history_valid("taaHistory")` (true once ≥1 frame has written it). The TAA executor
  uses this. The graph owns this knowledge, so the renderer never has to guess.
- **Synchronization (the part people get wrong):** last frame's write and this frame's read
  are in *different command buffers*. Submission order alone does not make memory writes
  visible to a later command buffer. You need an execution + memory dependency
  (color-attachment-write → fragment-shader-read). Two correct ways:
  1. The render pass's outgoing `VK_SUBPASS_EXTERNAL` dependency for transient color targets
     already covers `COLOR_ATTACHMENT_OUTPUT/WRITE → FRAGMENT_SHADER/SHADER_READ` and the
     `finalLayout` is `SHADER_READ_ONLY_OPTIMAL`. Check `vk_framebuffer.cpp`'s render pass
     creation. If so, the hazard is already covered, because external dependencies apply to
     all later commands on the queue.
  2. If not, emit an explicit image barrier for `previous_frame` reads at the start of the
     reading pass.
  Either way, **run with synchronization validation enabled**
  (`VK_LAYER_KHRONOS_validation` + `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`).
  It's the only reliable check here. A missing cross-frame barrier often "works" on NVIDIA
  and then fails on other hardware.
- **Frames in flight:** 2 physical copies is correct no matter how many frames are in flight.
  Frame N+1 reads what frame N wrote, and the GPU runs one queue in order. Don't allocate
  `k_max_frames_in_flight` copies. That's a common mistake, and it breaks the
  "previous = the other one" relationship.

**Ownership check against the invariants:** both images belong to the frame graph's transient
allocator, which is renderer-owned. Nothing goes on `VulkanContext`. ✔

**Verify B1 alone:** write a throwaway test pass that writes `frame_index % 2 ? red : green`
into a history resource, and a second pass that shows its `PreviousFrame` read. The viewport
must alternate red/green, **opposite** to what the writer wrote this frame. Delete the test
passes afterwards.

## B2. Velocity (motion vectors) in the G-buffer

### B2a. Add a real `RG16F` format

Don't use a half-empty `RGBA16F`, which wastes 4 bytes/pixel of G-buffer bandwidth on the
most bandwidth-bound pass in the frame. Add `RG16F` in three places:
- `framebuffer.h:8-22`: enum value (under "color").
- `vk_framebuffer.cpp:82-92`: `→ VK_FORMAT_R16G16_SFLOAT`.
- `frame_graph_loader.cpp:259-270`: parse `"rg16f"`.

Then grep for every `switch` on `FramebufferTextureFormat` (the ImGui texture-id path,
`read_pixel`, clear-value selection, any OpenGL mapping) and handle the new value, even if
it's just an assert in the OpenGL backend. A missing case there turns into a crash the first
time someone uses the format somewhere new.

**Why RG16F and not RG16_UNORM or RG8:** velocity in UV units is usually tiny (±0.01) but can
be large (fast pans). Half-float has relative precision, which fits that range. UE uses
encoded RG16 UNORM with a custom curve. That's a later optimization, not needed now.

### B2b. Attachment position

Insert `gVelocity` **before** `gDepth`, as color attachment index 4, so depth stays last:
- `scene_viewport_renderer.cpp:122-128`: `{RGBA8, RGBA16F, RGBA8, RED_INTEGER, RG16F, Depth}`.
- `:291-295`: register `gVelocity` as another alias of `m_gbuffer_framebuffer`.
- `deferred.hnfg`: new `gVelocity` `ImportedTarget/ExternalFramebuffer` resource, and
  `GBuffer`'s `Writes: [gAlbedo, gNormal, gPBRParams, gEntityID, gVelocity, gDepth]`. Update
  the comment block at the top of the file that describes the ordering.

**Then grep for anything that hardcodes "depth is attachment 4"**, e.g. `get_depth_attachment`,
`attachment_index == 4`, `Attachment: 4` in YAML, SSAO/lighting depth-view helpers. Moving
depth from index 4 to 5 is where this change can quietly break things.

**Clear value:** the G-buffer pass clears color to `[0,0,0,0]` (`deferred.hnfg`, `GBuffer`
`Clear:`). Zero velocity is the wrong default for anything that doesn't write velocity
(B2e). Handle it by having the resolve reconstruct camera motion from depth wherever no
geometry wrote velocity: detect it with `gEntityID == -1`, or add a 1-bit "velocity written"
flag in a spare channel. Don't rely on the clear value meaning anything.

### B2c. Previous-frame transforms per draw

**What:** `GPUDrawData` gets `glm::mat4 prev_model`, so both C++ (`gpu_types.h:111-117`) and
the GLSL struct (`Renderer3D_MeshletDeferred.glsl:13-19`, **and the identical struct in the
task stage and `Renderer3D_Meshlet.glsl`**) grow by 64 bytes. Also update any size checks,
e.g. the `* sizeof(GPUDrawData)` capacity math in `vk_renderer_api.cpp:174`.

**Where prev comes from:** keep a renderer-side cache in `Renderer3DData`, keyed by
**(entity_id, submesh pointer)**. One entity with several submeshes has several draws, and a
submesh's transform can include a node transform. Every frame:
- when building `draw_data` (`renderer_3d_meshlet.cpp:190-200`), look up the key. If found,
  `prev_model = cached`. If missing (first frame this draw exists), `prev_model = model`, i.e.
  no object motion, which is the correct default for a newly spawned object.
- write `model` into a *next* cache, and swap caches at end of frame. Swapping evicts draws
  that disappeared, so the map doesn't grow forever.

**Why renderer-side, not a scene component:** "previous transform" only means something to
the renderer's notion of frames, and there may be several viewports each with its own frame
cadence. UE keeps `PreviousLocalToWorld` on the render-side primitive for the same reason.
Physics sub-stepping and play/pause shouldn't have to know about it.

**Multiple viewports:** if more than one `SceneViewportRenderer` renders the same scene in a
frame, a single global cache mixes them up. Key the cache per view (see B3b: store it with
the rest of the per-view TAA state).

### B2d. Writing velocity in the meshlet G-buffer shader

**Mesh stage** (`Renderer3D_MeshletDeferred.glsl:106-119`): `gl_Position` stays as the
jittered `u_ViewProjection * model * pos`. Also output two new varyings:
- `v_clipCurr = u_ViewProjectionNoJitter * model      * pos`
- `v_clipPrev = u_PrevViewProjection   * prev_model * pos`

(Both matrices come from B3a.)

**Fragment stage:** new `layout(location = 4) out vec2 o_velocity;` (the depth attachment
isn't a color output, so it's 4, after `o_entityID` at location 3). Compute
`ndc = clip.xy / clip.w` for both, convert each to UV (`ndc * 0.5 + 0.5`, **with Vulkan's Y
direction already baked in by the correction matrix**), and write `uv_curr - uv_prev`.

**Why the divide happens in the fragment stage:** interpolating `clip.xy / clip.w` from the
vertex stage is wrong for perspective. The divide has to happen per fragment, after
perspective-correct interpolation of the clip-space vec4s. That's why you pass the full vec4s.

**Pipeline:** `renderer_3d_meshlet.cpp:43` → `resize(5, ...)`, and update the comment.
**Every other pipeline that draws into the G-buffer render pass must also declare 5 color
attachments**: the classic deferred-geometry pipeline, and the Renderer2D pipelines drawing
editor overlays through `post_scene_overlay_render`. A mismatch is a validation error
(`VUID-vkCmdDraw*-renderPass-*` / attachment count), not silent. Use it as your checklist.

### B2e. Things that don't write velocity (classic path, overlays, sky, cloth)

- **Classic `DeferredGeometry` path:** add the output and write camera-only velocity (use
  `model` for both current and previous). Per the invariants, don't add instance-rate
  `prev_model` attributes to a legacy path.
- **Editor overlays (Renderer2D):** they write nothing to location 4, so the value there
  stays whatever was cleared. The resolve's depth-based camera-motion fallback (B4b step 2)
  covers them.
- **Sky** (depth = far plane): the same fallback works, since reprojecting a far-plane point
  with `prevViewProj` gives rotation-only motion, which is correct for a sky at infinity.
- **Cloth (known limitation, write it down):** GPU-simulated cloth deforms without its
  transform changing, so B2c gives it camera-only velocity and it will smear a little under
  motion. The proper fix already has half its data: `clothStateA`/`clothStateB` ping-pong in
  the graph (`deferred.hnfg`, `ClothSeed`/`ClothSim`), so the cloth draw could read previous
  positions from the other buffer. Out of scope here. Neighborhood clipping keeps the smear
  bounded.

**Verify B2 alone:** add a debug output mode (reuse the lighting pass's or the resolve's)
that shows `abs(velocity) * 50` as RG. Expected: black when still; a smooth gradient over the
whole screen when orbiting; a uniform color on a moving rigidbody over a static background;
**no change when the jitter from B3 is on and the camera is still.** That last one shows the
velocity is jitter-free.

## B3. Camera jitter and the `CameraUBO` extension

### B3a. Share the `CameraUBO` declaration first (prep refactor)

Before adding fields, move the `CameraUBO` block into
`assets/shaders/include/camera.glsli`, or next to the existing `global_bindings.glsli`, and
`#include` it from all 9 shaders. Commit that on its own. It should be a zero-diff change in
output. It turns the next step into a one-file edit instead of 9 edits that can drift apart.

Then extend it. In C++ (`gpu_types.h:56-64`) and the new `.glsli`, **keep std140 alignment in
mind: mat4s are 16-byte aligned and a vec4 is safer than a vec2**:
- `mat4 view_proj_no_jitter`: current VP without jitter (velocity, and anything else
  unjittered that runs in a jittered pass).
- `mat4 prev_view_proj_no_jitter`: last frame's unjittered VP.
- `vec4 jitter`: `.xy` = this frame's jitter in UV units, `.zw` = last frame's.

`make_gpu_camera` (`vk_renderer_globals.cpp:11-26`) must apply the **same** clip correction to
the two new matrices as to `view_proj`. Otherwise velocity is computed in a different clip
space from rasterization, and you'll see a constant velocity with a vertical sign flip.

**Invariant check:** `CameraUBO` lives in `VulkanRendererGlobals`, which is renderer-owned.
This is per-view camera state, not pass-local data (see the exception called out in the
inherited invariants). ✔

### B3b. Where jitter and previous-frame state live

**Per-view TAA state** holds: jitter sequence index, previous unjittered VP, previous jitter,
the B2c transform cache, and a `history_reset` flag. It belongs to the *view*, i.e. the
`SceneViewportRenderer` instance or a `Renderer3DTAA::ViewState` object it owns. It does
**not** belong in a global static, and **never on `VulkanContext`**. UE calls this
`FSceneViewState`, and it's per-view for the same reason: an editor viewport and a game
viewport must not share history or jitter phase.

**Flow each frame:**
1. `SceneViewportRenderer` advances the view state before rendering: index++, compute
   jitter. Jitter is zero if `mode != taa`.
2. It passes the jitter (and prev VP) into `Scene::render`→`Renderer3D::begin_scene`. The
   signature at `renderer_3d.cpp:107` grows by a small `TemporalViewParams` struct. Don't add
   four more loose parameters.
3. `begin_scene` stores **both** `scene_projection` (unjittered) and
   `scene_projection_jittered` in `Renderer3DData` (`renderer_3d_internal.h:100-104`).
4. At end of frame the view state saves this frame's unjittered VP and jitter as "prev".

### B3c. Applying the jitter (and the Y sign)

**Sequence:** Halton(2, 3), 8 or 16 samples, remapped to [-0.5, 0.5] pixels. It's
low-discrepancy, so the samples cover the pixel evenly. A random sequence converges visibly
slower. 8 is the common default and 16 converges a little better on static images.

**Math:** convert pixel jitter to NDC: `jitter_ndc = 2 * jitter_px / viewport_size`. Then
offset the projection's clip-space x/y by `jitter_ndc * w`. In column-major GLM that means
adding it to `proj[2][0]` and `proj[2][1]`, because clip.x += jitter_ndc.x * clip.w and
clip.w comes from −z via column 2 for a perspective matrix. It's a sub-pixel translation in
screen space, not in view space.

**The GL→Vulkan Y flip:** you're jittering the GL-style matrix *before*
`make_gpu_camera`'s `correction[1][1] = -1`. So the rasterized jitter in Vulkan UV space has
**negated Y**. Pick one convention and document it in the code. The simplest is to store
`jitter` in the UBO already converted to *Vulkan UV space* (negate y there). Then the resolve
never has to think about it. If you get this wrong, the static image converges to a slightly
blurrier result and TAA looks "soft for no reason." That's hard to diagnose later, so check
it now (see verification).

**Orthographic cameras:** the same `[2][0]/[2][1]` trick doesn't work, because there's no w
dependence. Use `[3][0]/[3][1]` for ortho. Check `Camera::get_projection_matrix`
(`camera.cpp:62`) for the ortho branch and handle it, or explicitly disable jitter for ortho.

### B3d. Which consumer gets which matrix (applying the new invariant)

Go through every place that re-submits the camera and choose deliberately:

| Consumer | Matrix | Why |
|---|---|---|
| G-buffer raster (`scene.cpp` → meshlet/classic) | jittered | this is where the sub-pixel offset gets applied |
| Deferred lighting `u_InvViewProjection` (`renderer_3d_deferred.cpp:72-77`) | jittered | reconstructs position from the *jittered* depth. Unjittered gives a sub-pixel position error that shows up as lighting crawl. |
| SSAO (`renderer_3d_ssao.cpp:149-152`) | jittered | same reason |
| Shadow cascade fitting (`renderer_3d_shadow.cpp:476-478`) | **unjittered** | jitter here moves cascade bounds every frame and causes shadow shimmer |
| Culling (when Phase 6 lands) | unjittered | stable results |
| Vector icons / editor overlays after AA | unjittered | they're composited after the resolve |
| Post-process composite / output | either (it only uses exposure) | |

Put this table in a comment next to `scene_projection_jittered` in
`renderer_3d_internal.h`, so the next person adding a pass has to make the same choice.

**Verify B3 alone** (resolve not built yet, TAA mode on so jitter is active): the image
visibly wobbles by under half a pixel on edges. Shadows are **completely stable**. With
`mode = none`, output is bit-identical to pre-B3.

## B4. The resolve pass

### B4a. Placement: before bloom, in linear HDR

```
DeferredLighting → hdrColor
TAAResolve       reads hdrColor, gVelocity, gDepth, taaHistory(PreviousFrame), taaParams
                 writes taaHistory (this frame)
BloomThreshold   reads taaHistory   ← was hdrColor
PostProcessComposite reads taaHistory + bloomUp0   ← was hdrColor
```

**Why before bloom:** bloom then works on a temporally stable image, which directly helps
the firefly problem you've been fighting. Also, bloom is a blur, and doing TAA after bloom
would reproject a blurred signal and blur it again.

**Why HDR and not LDR:** in HDR, the neighborhood statistics are computed on physically
meaningful values, and exposure changes don't invalidate history. Its well-known weakness:
very bright pixels dominate the blend and leave trails. The fix is below (step 6), not a
move to LDR.

**Output is the history resource itself.** With B1 you don't need a separate output texture
plus a copy. The resolve writes `taaHistory` (current), downstream passes read `taaHistory`
(current), and next frame's resolve reads it back as `PreviousFrame`. One write, no copy.

**Mode ≠ TAA:** the pass stays in the graph. It outputs `hdrColor` unchanged (a copy, which
costs little at this resolution), and jitter is 0. That's the same "parameter, not graph
surgery" rule again.

### B4b. The algorithm (per pixel, in this order)

1. **Closest-depth velocity (dilation):** don't take velocity at this pixel alone. Find the
   texel with the *nearest* depth in the 3×3 (or a 5-tap cross) and use *its* velocity. At
   silhouettes this makes the foreground object's motion win over the background's, which
   removes most edge ghosting. (Karis 2014.)
2. **Camera-motion fallback:** where no geometry wrote velocity (B2b/B2e), reconstruct
   `prev_uv` from depth: world pos from the jittered inv-VP, then `prevViewProjNoJitter`,
   then UV.
3. **Reproject:** `prev_uv = uv - velocity`. If it's outside [0,1], reject history (use the
   current sample only).
4. **Fetch history with Catmull-Rom:** the optimized 5-tap version from Jimenez's Filmic
   SMAA. **Bilinear history fetch is the most common cause of "TAA makes everything soft."**
   Every frame's bilinear resample slightly low-pass filters the history, and that compounds
   over ~10 frames of accumulation. Clamp the result to ≥ 0 because Catmull-Rom has negative
   lobes.
5. **Neighborhood clip (variance clipping, Salvi 2016):** over the current frame's 3×3,
   accumulate the first and second moments in **YCoCg** (it decorrelates luma from chroma,
   so the box is tighter). `μ ± γσ` with `γ ≈ 1.0–1.25` defines an AABB. **Clip** history
   toward μ onto the AABB boundary (the ray-box intersection in Pedersen's talk). Don't
   **clamp** per channel: clamping moves colors off the μ→history line and causes hue
   shifts. Keep γ exposed.
6. **Blend with luma weighting:** `result = mix(current, clipped_history, feedback)` with
   `feedback ≈ 0.9` (TAAHistoryBlend in the parent doc). Weight both samples by
   `1 / (1 + luma)` before blending and undo it after (Karis's "tonemapped blend"). This is
   what stops a single 50.0 HDR sample from trailing for 10 frames. Lower the feedback a
   little as `|velocity|` grows (fast motion means history is less trustworthy), and set it
   to 0 when history is invalid or `prev_uv` was off-screen.
7. **Sanitize:** if the result is NaN or Inf, write the current sample instead (or 0).
   **This is mandatory, not polish.** A single NaN from a lighting edge case gets into
   history and spreads through the Catmull-Rom neighborhood until it covers the screen. Also
   sanitize the *input* at step 5's neighborhood gather.
8. **Current sample, unjittered (refinement, optional in v1):** the current pixel's sample
   was taken at a jittered position. Karis reconstructs it with a small Blackman-Harris
   filter over the 3×3, weighted by distance from the jittered sample positions. It's not
   needed for a first version, but it's the difference between "good" and "UE4-quality"
   static sharpness. Leave a TODO.

### B4c. Executor and resources

- New module `renderer_3d_taa.h/.cpp`, structured exactly like `renderer_3d_bloom.*`
  (`init/shutdown/execute_resolve/register_frame_graph_executors`, a per-render-pass pipeline
  cache map). Executor id `taa.resolve`. Register it with the others in
  `scene_viewport_renderer.cpp:95-99` + init, and add the `.cpp` to engine CMake.
- `taaParams` buffer: `feedback_min/max`, `clip_gamma`, `sharpness`, `int history_valid`,
  `int debug_view`. `history_valid` comes from `ctx.is_history_valid("taaHistory")`
  **and** the view state's `history_reset` flag. This buffer changes whenever reset happens,
  so check the per-frame-slot safety question from the inherited invariants here. If
  `set_data` isn't frame-slot safe, move `history_valid` into the push constant.
- Samplers: history needs **linear + clamp-to-edge** (for Catmull-Rom's bilinear taps).
  Neighborhood and depth need **nearest** (`texelFetch`).

## B5. Invalidation, sharpening, and editor behaviour

- **Reset history** on: viewport resize (the graph re-allocates, so B1's validity covers
  it), renderer-type switch (graph rebuild, same), AA mode change (set the view state's
  `history_reset`), and **camera cuts**. A cut is a teleport, an editor "focus on entity"
  snap, or a switch between editor camera and play camera (`editor_layer.cpp:90-117`). A
  simple heuristic: reset if the camera moved more than N meters or rotated more than M
  degrees in one frame. Also expose `Renderer3DTAA::request_history_reset(view)` for explicit
  cuts.
- **Sharpening:** TAA reliably softens the image a little. Add a cheap sharpen in
  `PostProcessComposite` *after* tonemap (a 5-tap unsharp mask, or AMD CAS if you want the
  standard one), controlled by `TAASharpness` (default ~0.2, 0 = off). Don't sharpen in HDR
  before tonemap, because it creates halos around highlights.
- **Paused/static scenes:** TAA converges to a supersampled image in ~8–16 frames. If the
  editor has (or later gets) any "skip rendering when idle" optimization, a static camera
  must keep rendering at least until the jitter cycle completes. Otherwise the viewport stays
  at frame-1 quality. This hasn't been checked yet; confirm it when you get to B5.
- **Picking stays exact:** entity id comes from `hdrEntityId`, which is written from the
  *jittered* `gEntityID`. That's at most half a pixel of offset on silhouettes, which is
  acceptable. Don't route entity id through TAA (it's an integer, and blending it is
  meaningless).

## B6. Settings and UI

Extend `AntiAliasingSettings` from A2:

| Setting | Default | Notes |
|---|---|---|
| `Mode` | `TAA` once stable (keep `FXAA` until then) | string enum |
| `TAAFeedback` | 0.9 | the parent doc's `TAAHistoryBlend`. The min feedback under motion is derived from it. |
| `TAAClipGamma` | 1.0 | variance clip box size |
| `TAAJitterSamples` | 8 | 8 or 16, Halton length |
| `TAASharpness` | 0.2 | post-tonemap sharpen |
| `TAADebugView` | Off | Off / Velocity / HistoryRejection / ClipAmount |

Same pattern as A2 (`settings.h` struct with `operator==`, string-serialized mode, UI combo
in `renderer_debug_panel.cpp`). Debug views are worth the time here. "Why is it ghosting" is
almost always answered by the rejection or clip view.

---

## What a naive version gets wrong (for interview-level understanding)

| Naive | Symptom | Production answer |
|---|---|---|
| Blend history with no neighborhood constraint | Heavy ghosting on everything that moves | Variance clipping in YCoCg (B4b.5) |
| Per-channel clamp instead of clip | Hue shifts and ghost trails that are tinted | Clip toward μ along the ray (B4b.5) |
| Bilinear history fetch | The image softens more the longer you look | Catmull-Rom 5-tap (B4b.4) |
| Velocity from this pixel only | Halos around moving silhouettes | Closest-depth dilation (B4b.1) |
| Linear-HDR blend | Bright specular leaves streaks | `1/(1+luma)` weighting (B4b.6) |
| Jitter everywhere | Shadow shimmer, unstable culling | Jittered/unjittered consumer table (B3d) |
| Velocity computed with jitter included | Still camera produces nonzero velocity, blurry result | Unjittered clip positions (B2d) |
| History as a hand-managed image | Barrier bugs, duplicated per temporal effect | Graph-owned `History` resources (B1) |
| No NaN guard | One bad pixel eventually blacks out the screen | Sanitize input and output (B4b.7) |
| `k_max_frames_in_flight` history copies | Reads a two-frame-old history, doubling the ghost distance | Exactly 2, swapped per executed frame (B1) |

---

## Deliberately out of scope (write down, don't build)

- **Editor overlays out of the G-buffer.** The Renderer2D overlays drawn via
  `post_scene_overlay_render` should move into a post-AA overlay pass, as the vector icons
  did in `ecb77d9`. Then they're never jittered and don't need velocity. It's a worthwhile
  cleanup, but separate work.
- **Cloth velocity** from the cloth ping-pong buffers (B2e).
- **Depth-based disocclusion** (keeping previous-frame depth as a second `History` resource
  and rejecting when depths disagree). Variance clipping handles most cases. Add it if you
  see persistent disocclusion trails. With B1 it's one more `History: true` resource.
- **TAAU / upscaling.** The same resolve with an output resolution different from the input
  resolution. B1 + B4 are the foundation. It needs `MatchSize` with a separate render-scale
  setting.
- **Forward and path-tracer graphs.** This plan changes `deferred.hnfg` only. The path tracer
  has its own accumulation/SVGF and must **not** get jitter. Gate jitter on
  `renderer_type == deferred`.

---

## Verification checklist

**Part A — FXAA**
- [ ] A1 split: with the output pass as copy + overlay, the output is bit-identical to pre-split
      (A/B screenshot) and picking is exact at silhouette edges and on vector icons.
- [ ] `Mode: None` → bit-identical to the A1 baseline.
- [ ] FXAA debug edge mask highlights geometric edges only: no flat-region noise, and dark
      edges aren't missed (if they are, check that luma is computed in gamma space).
- [ ] Sponza/Bistro: stair-stepping on long near-horizontal edges is visibly reduced. The
      screen border shows no wrap-around artifacts (the sampler clamp, like the `34ebd14` bug).
- [ ] Vector icons are pixel-sharp and unaffected by FXAA.
- [ ] Settings round-trip: change the FXAA values in the UI, save, restart, and the values
      are kept. Also check the bloom threshold/soft-knee load fix (`settings.cpp:217-222`).

**Part B — TAA**
- [ ] B1: the red/green test alternates opposite to the writer. No cycle diagnostic. Sync
      validation is clean for the history read/write.
- [ ] B1: resize the viewport. `is_history_valid` is false for exactly one frame, and there's
      no garbage flash.
- [ ] B2: velocity debug view is black with a still camera (jitter on), a smooth gradient
      when orbiting, and a solid color on a moving rigidbody. No validation errors about
      attachment counts on any G-buffer pipeline (meshlet, classic, Renderer2D overlays).
- [ ] B2: switch `GeometryPath` Meshlet ↔ Classic. Classic shows camera-only velocity and
      doesn't crash.
- [ ] B3: `Mode: None` is bit-identical to pre-B3. `Mode: TAA` without the resolve gives a
      sub-half-pixel wobble. **CSM shadows are completely still.**
- [ ] B3: jitter sign check. Set feedback to 0.97 and the Catmull-Rom path on, and hold the
      camera still. The image should converge to *sharper* than FXAA. If it converges softer,
      the Y sign of the stored jitter is flipped (B3c).
- [ ] B4: static camera converges in ≤16 frames. Thin geometry (meshlet silhouettes, Sponza
      banners' edges) stops crawling under slow camera motion.
- [ ] B4: a moving object over a detailed background leaves no ghost trail. Disocclusion
      (object moving to reveal background) clears within a few frames.
- [ ] B4: bloom firefly check. Sponza curtains with bloom on, slow pan: the sparkle field is
      substantially calmer than with FXAA. (Residual sparkle is roadmap 2e's job.)
- [ ] B4: NaN resilience. Temporarily inject a NaN in the lighting shader for one pixel for
      one frame. The screen recovers and there's no spreading black blob.
- [ ] B5: camera cut (focus-on-entity snap, edit ↔ play switch) shows no smear from the old
      view.
- [ ] Full pass: `RendererType` Deferred → Forward → Path Tracer → back, and AA mode cycles
      None → FXAA → TAA → None. No crash, no assert, no validation errors, and the path
      tracer image doesn't jitter.
- [ ] Profiling: `HN_GPU_SCOPE` timings for `TAA Resolve` and `Postprocess Output`. Expect
      well under 0.5 ms each at 1080p on your GPU. Anything larger usually means a
      non-`texelFetch` neighborhood or an unrolled loop that spilled registers.

> Tooling note: RenderDoc/NSight can't load descriptor-heap builds (see the heap-sampler
> investigation). For frame capture, use the `HONEY_DEBUG_AUTOSCENE` repro path and the debug
> views above.
