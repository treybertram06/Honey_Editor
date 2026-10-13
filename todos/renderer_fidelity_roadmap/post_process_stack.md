# Phase 2 — Post-Process Stack: Implementation Plan

Detailed breakdown of `roadmap.md` Phase 2 (Bloom, FXAA/TAA, GTAO, Color Grading). Written
against the codebase as of Sept 2026 (IBL/Phase 1 already shipped and wired — `IBLIntensity` in
`config/settings.yaml`, `Renderer3D_DeferredLighting.glsl` samples irradiance/prefiltered/BRDF-LUT).

## Invariants (hold across every step)

Inherited from `roadmap.md`, restated with the specifics that bite in this phase:

- **`VulkanContext` stays renderer-agnostic.** Every resource this phase introduces (HDR
  intermediate targets, bloom mip chain, FXAA/TAA/GTAO working textures, the color-grade LUT) is
  a `Renderer3D`/frame-graph-owned resource, never a `VulkanContext` member.
- **The frame graph is the only per-frame driver.** Every new pass below is a `deferred.hnfg`
  entry with a registered executor — no ad-hoc command buffer submission outside it.
- **No GPU-visible buffer is host-written while the GPU may read it.** Applies to every new
  per-pass `Buffer` resource this plan adds (bloom params, GTAO params, TAA jitter/history
  metadata, color-grade strength) — same staging + barrier discipline as `ssaoKernel`.
- **Every new feature gets a `config/settings.yaml` toggle + exposed parameter.** No hardcoded
  on/off. Concretely this means the exact triplet found at `settings.cpp:190-212` (load),
  `settings.cpp:294-295` (save), `renderer_debug_panel.cpp:313-321` (UI) — one `Settings::RendererSettings`
  field, one `DragFloat`/`Checkbox` call, nothing more exotic.
- **Phase-local addition — `FGSizeMode::MatchResource` does not chain.** `frame_graph.cpp:211-214`
  hard-errors if a `MatchSize` target is itself `MatchSize`. Every bloom mip resource must
  `MatchSize` the *same* base resource (`hdrColor`) with its own `Scale`, never the previous mip.
- **Phase-local addition — pass-local uniform data is a frame-graph `Buffer` resource, never a
  `global_bindings.h` entry.** `k_global_bindings` (set=0) is fixed, engine-wide, per-frame state
  (camera/lights/materials/shadows/environment) — not the mechanism for a pass's own parameters.
  Follow the `ssaoKernel` pattern exactly: `Type: Buffer` in `.hnfg`, `Usage: uniform` read
  binding, filled via `ctx.get_buffer(name)->set_data(...)`, lands on set=1 automatically through
  heap-mode reflection.
- **Phase-local addition — `AttachmentBlendState` (`pipeline_spec.h:38-44`) is currently just a
  bool.** There is no hardware src/dst blend factor support. Any "additive" step in this phase
  (bloom upsample accumulation) must sample-and-add in the shader, not rely on pipeline blend
  state. Do not spend time trying to configure hardware additive blending — it isn't wired up.

## Ground truth this plan depends on (verified against current code, not assumed)

- **`editorViewport` is the final LDR target** — `FramebufferSpecification` at
  `scene_viewport_renderer.cpp:109-113`: `{RGBA8, RED_INTEGER, Depth}`. `RGBA8` cannot hold HDR
  color. Every post-process pass before the very last one must write to a separate HDR resource.
- **Tonemap + gamma are currently baked into `Renderer3D_DeferredLighting.glsl`**, in *both*
  branches of `main()` — the sky/background branch (`:344-353`) and the lit branch (`:444-447`).
  Both call `aces_tonemap` then `pow(color, vec3(1.0/2.2))` before writing `o_color`. Both must
  lose the tonemap+gamma lines as part of Step 0.
- **Emissive is already decoded**, not a missing feature — `Renderer3D_DeferredLighting.glsl:319-321`
  unpacks emissive from `normal_samp.b/.a` (`unpack_rg` trick) and adds it into `color` at `:439`
  *before* tonemap. Bloom's source is therefore simply the post-lighting linear HDR `color` value,
  thresholded — no new G-buffer channel needed for bloom specifically (unlike TAA's motion
  vectors, see Step 3).
- **`flush_deferred_lighting` (`renderer_3d_deferred.cpp:48-97`) has zero hardcoded knowledge of
  `editorViewport`.** The pipeline's render pass/target comes entirely from whatever framebuffer
  the graph resolves for this pass's `Writes:` list (`ctx.get_pass_target_framebuffer()`,
  `:62-65`). Redirecting this pass to write an HDR intermediate instead of `editorViewport` is a
  **pure `.hnfg` YAML change** — no C++ edit to this file. Its pipeline declares 2 color
  attachments (`:26`, matching `o_color`/`o_entity_id`), depth test/write both off (`:23-24`).
- **`FramebufferTextureFormat` only has `RGBA8, RGBA16F, RED_INTEGER, DEPTH24STENCIL8,
  D32_SFLOAT`** (`framebuffer.h:8-22`). No 2-channel or R11G11B10-style bandwidth-friendly HDR
  format exists yet. `RGBA16F` is the only usable HDR format for every new intermediate texture
  in this phase.
- **`MatchSize: <name>` + `Scale: <factor>` is a single YAML entry** (`frame_graph_loader.cpp:222-239,
  442-462`) — `Scale` accepts one scalar (both axes) or `[x, y]`. A bloom mip chain is expressed as
  N resources all `MatchSize: hdrColor` with `Scale: 0.5, 0.25, 0.125, ...` — not N resources each
  matching the previous mip (see invariant above).
- **`vectorTexture`/`vectorEntityTexture`** (`deferred.hnfg:76-85`) is the existing precedent for
  "one Texture resource pair (`RGBA16F` + `red_integer`), one pass writing both, `MatchSize:
  gAlbedo`" — the exact shape needed for the new `hdrColor`/`hdrEntityId` pair in Step 0.
- **Imported textures are registered in `scene_viewport_renderer.cpp` right before graph
  compile** — the precedent is `ssaoNoise`: `scene_viewport_renderer.cpp:287-288`,
  `options.imported_textures.emplace("ssaoNoise", Renderer3DSSAO::get_noise_texture())`. A
  color-grade LUT asset follows the identical shape: load a `Texture2D` once, register it under a
  fixed resource name every frame the graph is (re)compiled.
- **GTAO is a shader-only swap on the existing SSAO pass**, confirmed against
  `Renderer3D_SSAO.glsl:78-152`: current algorithm reconstructs view-space pos from depth, builds
  a randomly-rotated TBN from a 4×4 noise texture, does 32 hemisphere-kernel samples
  (`u_SSAOKernel.u_Samples[32]`, radius/bias-scaled), depth-compares each, accumulates a binary
  occlusion term into `ssaoRaw.r`. The `ssaoKernel` buffer and `u_NoiseTexture` binding are both
  specific to this hemisphere-sample approach and become unnecessary once replaced by a horizon
  search. `ssaoRaw`/`ssaoTexture` are already `RGBA16F` (4 channels) but only `.r` is written today
  — a bent-normal side output (Phase 4 wants this) packs into `.gba` for free, no new resource.

---

## Step 0 — Post-process plumbing (prerequisite for every sub-item below)

This is the piece `roadmap.md` calls out explicitly: "get the plumbing right here even if you
only implement bloom first." Do this step alone first, verify pixel-identical output against the
pre-change build, and commit it standalone before touching bloom/AA/GTAO/grading.

**0a. New frame-graph resources** (`assets/frame_graphs/deferred.hnfg`):
```yaml
hdrColor:
  Type: Texture
  Format: RGBA16F
  MatchSize: gAlbedo
  Layers: 1
hdrEntityId:
  Type: Texture
  Format: red_integer
  MatchSize: gAlbedo
  Layers: 1
```
(Mirrors the existing `vectorTexture`/`vectorEntityTexture` pair exactly.)

**0b. Retarget `DeferredLighting`'s `Writes:`** from `[editorViewport]` to `[hdrColor,
hdrEntityId]`. No other field of that pass changes.

**0c. Strip tonemap out of `Renderer3D_DeferredLighting.glsl`.** Delete the two
`bg_color *= u_Camera.u_Exposure; bg_color = aces_tonemap(bg_color); bg_color =
pow(bg_color, vec3(1.0/2.2));` lines (`:351-353`) and the equivalent `color *=
u_Camera.u_Exposure; color = aces_tonemap(color); color = pow(color, vec3(1.0/2.2));` lines
(`:445-447`). Both branches now write linear HDR `o_color` straight through (exposure moves to
the new composite pass — see 0e). Leave `aces_tonemap()` the function defined but unused here;
it moves to the new shader in 0d.

**0d. New pass + shader: `PostProcessComposite`.** New file
`Honey/engine/src/Honey/renderer/renderer_3d/renderer_3d_postprocess.cpp/.h`, structured exactly
like `renderer_3d_ssao.h/.cpp` (static `init`/`shutdown`, `register_frame_graph_executors`,
pipeline cached per render-pass-native-handle via `PipelineSpec::from_shader(...)` +
`Pipeline::create_heap_mode`). Register executor id `postprocess.composite`.

New shader `assets/shaders/Renderer3D_PostProcessComposite.glsl` (fullscreen triangle, same
vertex stage as `Renderer3D_DeferredLighting.glsl:1-18`):
```glsl
layout(set=1, binding=0) uniform sampler2D  u_HDRColor;
layout(set=1, binding=1) uniform isampler2D u_EntityId;
layout(location=0) out vec4 o_color;
layout(location=1) out int  o_entity_id;

void main() {
    vec3 color = texture(u_HDRColor, v_uv).rgb;
    // (bloom add, exposure, tonemap, gamma, color-grade LUT all land here in later steps)
    color *= u_Camera.u_Exposure;
    color  = aces_tonemap(color);
    color  = pow(color, vec3(1.0/2.2));
    o_color = vec4(color, 1.0);
    o_entity_id = texelFetch(u_EntityId, ivec2(gl_FragCoord.xy), 0).r;
}
```

New `.hnfg` pass:
```yaml
- Name: PostProcessComposite
  Executor: postprocess.composite
  Queue: graphics
  ReadBindings:
    - { Name: hdrColor,    Usage: sampled, Shader: u_HDRColor }
    - { Name: hdrEntityId, Usage: sampled, Shader: u_EntityId, ViewKind: Color2D }
  Writes: [editorViewport]
  Clear:
    color: [0.1, 0.1, 0.1, 1.0]
```
This becomes the new last pass in the graph, replacing `DeferredLighting` as the
`editorViewport` writer.

**0e. `EditorCameraExposure`/`u_Camera.u_Exposure` needs to still reach this shader.** It's
already part of the global `CameraUBO` (set=0) that every pass submits via
`VulkanRendererAPI::submit_camera` — no new plumbing, `PostProcessComposite`'s executor just
needs the same `#include "global_bindings.glsli"` + `CameraUBO` block every other lighting shader
has.

**0f. Register the new executor.** `scene_viewport_renderer.cpp:44` (alongside
`Renderer3DSSAO::register_frame_graph_executors()`), add
`Renderer3DPostProcess::register_frame_graph_executors()`. Add the new `.cpp` to the engine CMake
source list.

**Verification (mandatory before moving to 2a):** Build, load the Bistro/Sponza test scene,
A/B screenshot against the pre-Step-0 build. Output must be **pixel-identical** — this step is a
pure refactor (move tonemap from one shader to another, add one indirection), not a visual
change. Any difference is a porting bug (most likely: exposure applied twice, or gamma applied
twice, or `hdrEntityId`'s `texelFetch` vs `texture` sampling mismatch causing off-by-one entity
picking near silhouette edges — test picking in the same pass).

---

## Step 1 — Bloom (2a)

Builds on Step 0's `hdrColor`. Dual-Kawase-style: threshold, downsample chain, upsample chain
(additive-by-shader, see blend-state limitation above), composite.

**1a. New resources** (all `MatchSize: hdrColor`, `Format: RGBA16F`, `Scale` per level —
remember: every one matches `hdrColor` directly, never the previous mip):

| Resource | Scale | Role |
|---|---|---|
| `bloomMip0` | 0.5 | threshold + first downsample |
| `bloomMip1..bloomMip5` | 0.25, 0.125, 0.0625, 0.03125, 0.015625 | downsample chain |
| `bloomUp4..bloomUp0` | same scales as `bloomMip4..bloomMip0` | upsample-accumulate chain |

(6 mips is the roadmap's "5-6 times" figure; trivially fewer by dropping the tail entries if
1-2 days proves optimistic — each level is one resource + one pass, independently prunable.)

**1b. New Buffer resource `bloomParams`** (`Type: Buffer`, small UBO: `threshold`, `soft_knee`,
`strength`), same `ssaoKernel`-style fill-on-instance-change pattern.

**1c. Passes** (all executor id shared across instances — one C++ function per shader,
resolved generically via `bind_heap_pipeline`'s reflection, so N mip levels reuse 1 executor):

- `BloomThreshold` — reads `hdrColor` + `bloomParams`, writes `bloomMip0`. Shader
  `Renderer3D_BloomThreshold.glsl`: box-downsample (4-tap or 13-tap Kawase-style) combined with a
  soft-knee luminance threshold (Karis-style: `contribution = max(0, luma - threshold) /
  max(luma, eps) * color`).
- `BloomDownsample1..5` — 5 passes, each reads the previous `bloomMip[i-1]` (shader binding
  `Shader: u_Source` overriding the resource-name default so all 5 reuse one shader file
  `Renderer3D_BloomDownsample.glsl`), writes `bloomMip[i]`. Plain 13-tap Kawase downsample, no
  threshold (already applied at mip0).
- `BloomUpsample4..0` — 5 passes, reverse order, each reads the smaller `bloomMip[i+1]`/`bloomUp[i+1]`
  (whichever is the "already upsampled" source — `bloomMip5` for the first one) **and**
  `bloomMip[i]` (same-resolution detail to blend back in), writes `bloomUp[i]`. Shader
  `Renderer3D_BloomUpsample.glsl`: `o_color = texture_upsampled(u_Low) + texture(u_Detail)` — the
  "additive blend" happens as an in-shader sum since there's no hardware additive blend state.

**1d. Composite into `PostProcessComposite`.** Add `{ Name: bloomUp0, Usage: sampled, Shader:
u_Bloom }` to that pass's `ReadBindings`, and in the shader, *before* tonemap: `color +=
u_BloomParams.strength * texture(u_Bloom, v_uv).rgb;` — composite pre-tonemap per
`lighting_improvements.md`'s note (bloom must participate in HDR compression).

**1e. Settings/UI.** `Settings::RendererSettings`: `BloomThreshold` (default ~1.0),
`BloomSoftKnee` (~0.5), `BloomStrength` (~0.04), `BloomEnabled` (bool). Triplet at
`settings.cpp`/`renderer_debug_panel.cpp` per the pattern above. `BloomEnabled` gates by zeroing
`strength` when off (cheapest correct "disable" given the graph is static — see FXAA note below
for the alternative if a real pass-skip is later wanted).

---

## Step 2 — FXAA (2b stopgap)

Single new pass after `PostProcessComposite`, operating on final LDR/gamma-space color (FXAA's
original NVIDIA formulation is designed for post-tonemap 8-bit color).

**2a.** Retarget `PostProcessComposite`'s `Writes:` from `[editorViewport]` to a new intermediate
`ldrColor` (`Format: RGBA8`, `MatchSize: gAlbedo`) + keep `hdrEntityId` passthrough on a paired
`ldrEntityId` (or just keep writing `hdrEntityId` again — entity id doesn't need AA).

**2b.** New pass `FXAA`, executor `postprocess.fxaa`, reads `ldrColor` (+ entity-id passthrough),
writes `editorViewport`. Standard single-pass luma-edge-detect FXAA (Timothy Lottes' public
formulation) — no new resources beyond `ldrColor`.

**2c.** `AntiAliasing: FXAA | None` in settings (string enum like `RendererType`/`GeometryPath`,
not a bool — TAA joins this enum in Step 3). While `None`, the FXAA shader early-outs to a pure
copy (cheap, keeps the graph static and toggle-free at the YAML level, consistent with the
"parameter not graph surgery" approach used for `BloomEnabled`).

---

## Step 3 — TAA (2b real target)

**Flagged as the highest-risk item in this phase — confirmed zero precedent for a G-buffer
format change anywhere in this codebase's history** (gbuffer_spec has been `{RGBA8, RGBA16F,
RGBA8, RED_INTEGER, Depth}` unchanged since the deferred path landed). Recommend shipping Steps
0/1/2/4/5 first, land TAA as its own follow-up plan once FXAA is stable and this phase's plumbing
has been exercised in real use. Sequencing sketch (do not treat as final — the motion-vector
format decision below needs to be made at implementation time, not guessed here):

**3a. Motion-vector G-buffer attachment — decision point.** `FramebufferTextureFormat` has no
2-channel format (`framebuffer.h:8-22`). Two options, pick one before writing code:
  - Add a 6th G-buffer attachment using `RGBA16F` (only 2 channels used, `.rg` = motion vector,
    `.ba` unused/wasted) — simplest, costs G-buffer bandwidth for 2 dead channels.
  - Extend `FramebufferTextureFormat` with a real `RG16F` entry first — more correct, touches the
    format enum + whatever Vulkan format-mapping table backs it (not yet located; grep
    `FramebufferTextureFormat::RGBA16F` usage in `platform/vulkan/` at implementation time to find
    the mapping table before choosing this path).
  Either way this is a `gbuffer_spec.attachments` change (`scene_viewport_renderer.cpp:116-121`)
  and a new named `Writes:` entry across every pass that writes the G-buffer (`GBuffer` in
  `deferred.hnfg`, plus `Renderer3D_DeferredGeometry.glsl`/`Renderer3D_MeshletDeferred.glsl` gain
  an output).

**3b. Per-frame camera jitter.** Sub-pixel offset applied to the projection matrix. Per the
`renderer_globals_migration.md` ownership split, this is `VulkanRendererGlobals`-owned per-frame
data (it's view-uniform-adjacent, not a `VulkanContext` concept) — jitter offset computed
alongside `CameraUBO` construction wherever that's currently built for each pass
(`renderer_3d_deferred.cpp:72-77` and its SSAO/shadow siblings), added as a small offset to the
projection before `submit_camera`.

**3c. History buffer + reprojection.** New persistent (not transient-per-frame-graph-rebuild)
`RGBA16F` texture holding last frame's composited HDR color, same resolution as `hdrColor`. Needs
its own lifetime management outside the frame graph's per-rebuild transient allocator (the graph
reallocates transient textures on resize/rebuild — a TAA history buffer must survive across
frames untouched except by the TAA pass itself, closer to how `ssaoNoise`/BRDF LUT are
externally-owned resources fed in via `imported_textures` than a plain `Texture:` graph resource).

**3d. New pass `TAAResolve`**: reads `hdrColor` (current), the persistent history buffer, and the
new motion-vector G-buffer channel; writes a resolved `hdrColor` (or an `hdrColorResolved`
alias) with neighborhood-clamping variance clipping against history, using motion vectors for
reprojection and rejecting on large depth/motion deltas (falls back to current-frame sample).
Slots between `DeferredLighting`/bloom-composite and `PostProcessComposite`.

**3e. Settings/UI.** Extend the `AntiAliasing` enum from Step 2 with a `TAA` value.
`TAAHistoryBlend` (~0.9) as the tunable.

**Ordering note (from roadmap.md):** land this before or alongside GTAO if both are in flight —
GTAO's horizon-search noise "becomes invisible with TAA," so shipping GTAO first without TAA
means a visibly noisier interim state that TAA later cleans up. Not a hard blocker, just don't be
surprised by transient noise.

---

## Step 4 — GTAO (2c)

Shader-only swap on the existing SSAO pass — same resources, same pass names, same bindings
(`gNormal`, `gDepth` as today), zero `.hnfg` changes beyond possibly repurposing `ssaoKernel`.

**4a. Rewrite `Renderer3D_SSAO.glsl`'s fragment stage** (keep filename/pass/executor id
`ssao.draw` — roadmap explicitly frames this as "not a new pass"). Delete the 32-sample kernel
loop and TBN-from-noise-texture construction (`:78-129` region). Replace with horizon-based
search (Jimenez et al., Activision 2016): for 2-4 slice directions around the view-space normal's
tangent plane, step along the depth buffer per slice, find the horizon angle, integrate visibility
analytically. Reuses `u_gNormal`/`u_gDepth` exactly as now.

**4b. Repurpose (not delete) the `ssaoKernel` Buffer resource** — rename its fields from
`SSAOKernelUBOData{samples[32], radius, bias, noise_scale}` to GTAO's actual parameters (slice
count, step count per slice, radius, power/falloff). Keeps the existing fill-on-instance-change
mechanism in `Renderer3DSSAO::execute_draw` (`renderer_3d_ssao.cpp:161-184`), just changes what
gets written into it.

**4c. Noise texture becomes optional.** GTAO wants a small per-pixel rotation/offset jitter for
the slice directions (avoids banding) — the existing 4×4 `ssaoNoise` texture can be reused for
this directly (rotate slice angle by a value sampled from it), so `u_NoiseTexture`'s binding can
stay; only the *use* of it changes (rotation angle instead of TBN basis).

**4d. Bent normal side output — free, no new resource.** `ssaoRaw`/`ssaoTexture` are already
`RGBA16F`; pack AO into `.r` (as today) and the horizon-search's bent normal into `.gb`
(oct-encoded, same helper already used for `u_gNormal` in the G-buffer). `SSAOBlur`
(`Renderer3D_SSAOBlur.glsl`) needs to blur `.gb` alongside `.r` — cheap at this resolution, but
don't forget it; an unblurred bent normal reintroduces per-pixel noise Phase 4's IBL specular
occlusion consumer would otherwise avoid.

**4e. Settings/UI.** `SSAORadius`/`SSAOFalloff`/`SSAOSliceCount` replace the old kernel-count
knob (if one was exposed) — same triplet pattern. Rename in UI from "SSAO" to "Ambient Occlusion"
or keep "SSAO" label for continuity; either is fine, just be consistent with whatever the current
debug-panel label says (check `renderer_debug_panel.cpp` at implementation time).

---

## Step 5 — Color Grading (2d)

Cheapest item, folds entirely into `PostProcessComposite` — no new frame-graph pass.

**5a. LUT asset format — decision point.** Verify whether the engine has `Texture3D` support
(grep at implementation time; not confirmed in this research pass). If not, use the standard
2D-strip encoding (a 32³ LUT baked as a 1024×32 2D texture, 32 tiles of 32×32 laid out
horizontally) — the universal trick for engines without native 3D texture sampling.

**5b. Register as an `ImportedTexture`**, same shape as `ssaoNoise`
(`scene_viewport_renderer.cpp:287-288`): load the LUT PNG/asset into a `Texture2D` once (on scene
load / LUT-asset-reference change, not per-frame), `options.imported_textures.emplace
("colorGradeLUT", lut_texture)`. New `.hnfg` resource: `colorGradeLUT: Type: ImportedTexture`.

**5c. Composite shader addition** (`Renderer3D_PostProcessComposite.glsl`, after tonemap+gamma,
before final `o_color` write):
```glsl
vec3 graded = sample_lut_2d_strip(u_ColorGradeLUT, color, 32.0); // 32 = LUT size
color = mix(color, graded, u_ColorGradeStrength);
```

**5d. Settings/UI.** `ColorGradeLUTPath` (asset reference, per-scene — not a global setting;
follows whatever pattern `SkyboxComponent`'s environment reference already uses, since this is
scene-data not engine-config) + `ColorGradeStrength` (float 0-1, this one *is* a global/engine
setting via the usual triplet).

---

## Settings/UI additions across this phase (summary)

All follow the identical `settings.cpp` load (`~:190-212`) / save (`~:294-295`) /
`renderer_debug_panel.cpp` UI (`~:313-321`) triplet:

| Setting | Type | Step |
|---|---|---|
| `BloomEnabled` / `BloomThreshold` / `BloomSoftKnee` / `BloomStrength` | bool / float×3 | 1 |
| `AntiAliasing` (`None \| FXAA \| TAA`) | string enum | 2, 3 |
| `TAAHistoryBlend` | float | 3 |
| `SSAORadius` / `SSAOFalloff` / `SSAOSliceCount` | float×2, int | 4 |
| `ColorGradeStrength` | float | 5 |
| `ColorGradeLUTPath` | scene-level asset ref, not a global setting | 5 |

---

## Suggested implementation order

1. **Step 0** (plumbing) — standalone commit, A/B-verified pixel-identical.
2. **Step 1** (bloom) — highest visual impact, fully self-contained once Step 0 lands.
3. **Step 2** (FXAA) — cheap, ships same week.
4. **Step 4** (GTAO) — independent of AA choice, can land in parallel with Step 2/before Step 3.
5. **Step 5** (color grading) — trivial once Step 0 exists, do whenever convenient.
6. **Step 3** (TAA) — deliberately last; largest scope, only G-buffer-format change in this phase,
   benefits from bloom/GTAO already being stable so TAA's history-buffer verification isn't
   fighting other in-flight changes at the same time.

## Verification checklist

- [ ] Step 0: A/B screenshot, pre- vs post-, pixel-identical. Entity-id picking still exact
      (test near silhouette edges, not just flat surfaces).
- [ ] Bloom: toggle `BloomEnabled` live, lamp posts / emissive strips bleed correctly; strength=0
      is bit-identical to bloom disabled (confirms the "gate via param not graph surgery"
      approach is truly a no-op at zero).
- [ ] FXAA: edges visibly smoother on thin geometry (meshlet silhouettes), `AntiAliasing: None`
      reproduces pre-FXAA output exactly.
- [ ] GTAO: A/B against old SSAO on a scene with strong directional occlusion cues (corners,
      crevices) — GTAO should show correct directional falloff old SSAO's hemisphere sampling
      didn't capture. Bent normal sanity-check: render it as debug output, should point away from
      nearby occluders.
- [ ] Color grading: LUT swap live-changes scene mood without a shader rebuild; `Strength: 0`
      bit-identical to ungraded.
- [ ] TAA (when implemented): camera motion doesn't ghost/smear on moving objects; disocclusion
      edges (object passing in front of background) don't leave trailing artifacts; static camera
      converges to a cleaner image than FXAA on thin/aliased geometry.
- [ ] Full pass: `RendererType` switch Deferred → Forward → Path Tracer → back — no crash, no
      assert (this phase's new passes are Deferred-only; Forward/PathTracer graphs are untouched
      by this plan, confirm they still compile/run).
