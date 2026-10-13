# VulkanRendererGlobals: Ownership Migration of the Set-0 Globals Machinery

## Motivation

The C5 regression (full post-mortem in `geometry_pipeline_cleanup.md`) happened because the
set-0 globals machinery — camera, lights, materials, tiled lighting, shadow matrices — lives
scattered across `VulkanContext`, with load-bearing buffers hidden inside functions named after
machinery that was being deleted. The hotfix (`create_global_buffer_resources`,
`vk_context.cpp:402`) restored the buffers but entrenched the underlying problem: renderer data
owned by the device-abstraction layer.

The ownership discussion that followed settled the model, aligned with how established engines
split this (Unreal: RHI owns devices/heaps, GPUScene and view uniforms live in the Renderer
module; Godot: `RenderingDevice` vs `RendererSceneRenderRD`):

- **Mechanism vs. policy.** The device layer (`VulkanContext`: device, queues, swapchain, sync,
  frame lifecycle) and the bring-up layer (`VulkanBackend`: instance, device selection,
  descriptor heap) own no semantically-named data. The descriptor heap stays on the backend —
  slot allocation is mechanism.
- **A new `VulkanRendererGlobals` object owns the data plane of the heap's set-0 bindings**: the
  device-local globals buffer, its CPU mirror and staging, `PendingGlobals`, the materials SSBO,
  and the shadow-matrix mirrors. Its schema is `k_global_bindings`
  (`Honey/renderer/global_bindings.h`) — every noun in that table is a renderer concept, so the
  whole system moves, not just the pieces the incident exposed.
- **The high-level `Renderer` owns the object** — not `Renderer3D`, whose resources are
  rebuilt with the frame graph. `VulkanRendererGlobals` has device lifetime and is shared
  infrastructure: `Renderer3D`, `DebugRenderer3D`, and the future Renderer2D rewrite all consume
  it (the multi-client argument that put the heap on the backend).
- **Explicit frame orchestration, no callbacks.** `Renderer` already brackets the frame
  (`renderer.cpp:107-111` calls `begin_frame_recording()`); it gains the two globals calls
  directly instead of `VulkanContext` calling upward through hooks.

Two knowingly-deferred items, recorded so they aren't re-litigated: the SDF **font machinery**
(`m_font_set_layout`, band/curve UBOs) remains renderer-named `VulkanContext` state until the
Renderer2D rewrite claims it — this migration does not finish cleaning the context, and says so.
And **materials** stay inside `VulkanRendererGlobals` for now; if a real material/asset system emerges
later, the materials buffer is the natural piece to graduate out (mirroring how mature engines
separate material storage from view constants).

## Invariants (hold across every step)

- **`VulkanContext` ends with zero renderer-named members or methods.** The parent invariant's
  fitness check applies with no globals exemption — "shadow", "materials", "camera", "lights"
  on a context member is the smell. (Exception, documented above: the font machinery, parked
  until the Renderer2D rewrite.)
- **The heap stays on `VulkanBackend`; `k_global_bindings` stays the single source of truth for
  set 0.** No side-channel binding writes; the hardcoded binding index `2` in the materials path
  gets fixed en route, not copied forward.
- **`VulkanRendererGlobals` has device lifetime.** Created once in `Renderer::init`, destroyed once in
  `Renderer::shutdown`. Frame-graph rebuilds, swapchain recreation, and `RendererType` switches
  must not touch it — `VulkanDescriptorHeap::register_global_binding` is once-only
  (`vk_descriptor_heap.cpp:562-567` asserts on re-register), so any lifecycle that re-inits it
  without a backend restart is a bug by construction.
- **After Part C, the materials heap slot is written once, at creation, and its address never
  changes.** A per-frame binding rewrite reappearing means the migration regressed.
- **No GPU-visible buffer is host-written while the GPU may read it.** Device-local + per-frame
  staging + the existing barrier pattern, never host-visible memory read directly by shaders.
- **Init-order guards become asserts, never silent returns.** A missing load-bearing resource
  must fail loudly; silent degradation is what turned the C5 bug into a debugging session.

---

## Part A — Dead deletions

Pure deletion, no behavior change, independently committable. All targets verified dead against
the current tree.

**A1. Shadow uploads keep only their live halves** — `vk_context.cpp:946-986`
In `upload_shadow_matrices` and `upload_directional_shadows`: delete the early-return guard and
the `vkMapMemory`/`memcpy`/`vkUnmapMemory` half (the deleted classic descriptor set was the only
reader of those per-frame buffers). The `m_globals_cpu` mirror memcpy becomes the whole body;
replace its emptiness check with `HN_CORE_ASSERT` — after init it can never legitimately be
empty.

**A2. The hotfix creator shrinks to materials-only** — `vk_context.cpp:402-489`
Remove the `shadow_matrices` and `dir_shadow_matrices` entries from
`create_global_buffer_resources` / `cleanup_global_buffer_resources`, and delete the
`m_shadow_matrices_ssbos` / `m_dir_shadow_ssbos` member trios (`vk_context.h:362-368`).

**A3. Never-referenced members and declarations** — `vk_context.h`
Delete the `m_camera_ubos` / `m_lights_ubos` / `m_tiled_lighting_ssbos` trios (`:346-360`; zero
`.cpp` references) and the two declarations with no definition anywhere:
`set_shadow_cubemap_resources` (`:44`) and `set_dir_shadow_resources` (`:49`).

Verify: `grep -rn "m_camera_ubo\|m_lights_ubo\|m_tiled_lighting_ssbo\|m_shadow_matrices_ssbo\|m_dir_shadow_ssbo\|set_shadow_cubemap_resources\|set_dir_shadow_resources" engine/src` → zero hits.

## Part B — Extract `VulkanRendererGlobals` (verbatim move, zero behavior change)

The checkpoint commit: everything relocates, nothing is redesigned. Renders identically before
and after.

> **Implemented.** Two things landed differently from the text below, both deliberate:
>
> - The class is `VulkanRendererGlobals`, not `RendererGlobals` — every sibling type in
>   `platform/vulkan/` carries the prefix (`VulkanContext`, `VulkanBackend`,
>   `VulkanDescriptorHeap`, `VulkanRendererAPI`). Filenames are unchanged. Parts C and D below
>   have been updated to match.
> - **B3's pairing needs a guard the plan didn't call for.** `begin_frame_recording` has two
>   early-return paths (fence-wait failure, `VK_ERROR_OUT_OF_DATE_KHR`) where recording never
>   becomes active, so both `begin_frame()` and `snapshot()` are gated on `vk->is_recording()`,
>   and `Renderer::end_frame()` is the last statement *inside* `Application::run`'s
>   `if (!m_minimized)` block rather than after it. Without both, the R1 mitigation assert fires
>   on window resize and on minimize/restore.
>
> Also implemented: the shadow setters dropped their `frame` parameter (they write host state
> only), and `make_gpu_camera` moved with `flush_pending` — it was a `vk_context.cpp` file-static
> with no other caller, which the table below omits.

**B1. The class** — new files `engine/src/platform/vulkan/vk_renderer_globals.h/.cpp`, class
`VulkanRendererGlobals`. It is Vulkan-specific, like the heap — `renderer.cpp` already makes direct
`vk->` calls (`:107-111`, `:140-142`), so `Renderer` holding a Vulkan type is existing idiom.
Init takes `VulkanBackend*`: device, physical device, and heap all come from backend accessors
(`vk_backend.h:57-62`), and `GlobalsLayout::build` gets its limits from
`vkGetPhysicalDeviceProperties` on `backend->physical_device()`. The frame index is passed as a
parameter by every caller — the class has **no `VulkanContext` dependency**. (It references
`VulkanContext::k_max_frames_in_flight` for array sizing; that constant is device-layer
mechanism and stays put.)

State that moves in (from `vk_context.h`): `m_globals_buffer`, `m_globals_alloc`,
`m_globals_cpu`, `m_globals_staging[]`, `m_globals_staging_alloc[]`, `m_globals_staging_mapped[]`,
`m_globals_layout` (`:338-344`), `struct PendingGlobals` + `m_pending_globals` + accessors
(`:102-128`, `:391`), and — temporarily, until Part C kills them — the hotfix materials buffers
(`:354-356`).

Code that moves in (from `vk_context.cpp`), renamed where the old name described the deleted
classic half:

| Old (`VulkanContext`) | New (`VulkanRendererGlobals`) |
|---|---|
| `create_global_descriptor_heap_resources` body (`:738-833`) | `init(VulkanBackend*)` — buffer + staging creation, heap slot registration |
| `cleanup_global_descriptor_heap_resources` (`:873-893`) | `shutdown()` |
| `record_globals_upload` (`:835-871`) | part of `begin_frame(cmd, frame)` |
| `m_pending_globals.reset()` call (`:1243`) | part of `begin_frame(cmd, frame)` |
| snapshot memcpy in `end_frame_recording` (`:1290-1292`) | `snapshot(frame)` |
| `flush_globals_to_heap` (`:1672-1701`) | `flush_pending(frame)` |
| `upload_shadow_matrices` (post-A1) | `set_shadow_matrices(const ShadowMatricesSSBO&)` |
| `upload_directional_shadows` (post-A1) | `set_directional_shadows(const DirectionalShadowSSBO&)` |
| `write_materials_heap_binding` (`:934-944`) + hotfix buffer creation (`:402-463`) | private, dies in Part C |

The barrier code in `record_globals_upload` moves verbatim — it uses only global
`VkMemoryBarrier`s, no queue-family state (`:847-870`), and its correctness argument (all
graphics submits serialize under the backend's shared graphics mutex, comment at `:843-846`)
is unchanged by the move. Same for the fence-wait safety argument on the snapshot
(comment `:1285-1289`).

**B2. Ownership and lifecycle** — `renderer.cpp`
File-static `Scope<VulkanRendererGlobals>` in `renderer.cpp`, exposed via a `Renderer::globals()`
accessor (`renderer.h`; forward-declare the class to keep Vulkan headers out).
- **Create** in `Renderer::init` (`renderer.cpp:26-53`): after `RenderCommand::init()` (`:31`),
  **before `Renderer3D::init()`** (`:36`) — pipeline creation asserts registered global bindings
  (`vk_descriptor_mapping.cpp:119-127`), so registration must precede the first engine pipeline.
  Backend pointer via `Application::get().get_vulkan_backend()` (existing pattern,
  `vk_renderer_api.cpp:267`).
- **Destroy** in `Renderer::shutdown` (`renderer.cpp:55-94`): after the existing
  `ctx->wait_idle()` (`:80-85`), before `RenderCommand::shutdown()` (`:87`). This keeps
  destruction ahead of `VulkanContext::destroy()` (window teardown) and `VulkanBackend::shutdown`
  (application dtor order, `engine.cpp:94-116`).

**B3. Frame orchestration** — `renderer.cpp`, `engine.cpp`
- `Renderer::begin_frame` (`renderer.cpp:107-111`): immediately after
  `vk->begin_frame_recording()`, call
  `globals->begin_frame(vk->get_recording_cmd(), vk->get_current_frame())` — pending reset +
  upload recording. The upload copy remains the first work in the frame: `begin_frame_recording`
  ends with heap begin/bind (`vk_context.cpp:1220-1221`) and nothing else records into the
  command buffer before `Renderer::begin_frame` returns. (The GPU-profiler reset that previously
  recorded *after* the globals copy now records before it — a query-pool reset, order-irrelevant.)
- New `Renderer::end_frame()`: calls `globals->snapshot(vk->get_current_frame())`. Wire into
  `Application::run` between the ImGui pass end (`engine.cpp:203-208`) and
  `m_window->on_update()` (`engine.cpp:213`) — i.e., after all producers, before
  `swap_buffers()` → `end_frame_recording()` submits.

**B4. Producer wiring** — `vk_renderer_api.h/.cpp`, `renderer_3d_shadow.cpp`
- Add `VulkanRendererAPI::set_globals(VulkanRendererGlobals*)`, a static mirroring
  `set_recording_context` (`vk_renderer_api.cpp:26-28`), called from `Renderer::init` right
  after constructing the object.
- All eight `pending_globals()` touchers (`vk_renderer_api.cpp:270-350`: `submit_camera`,
  `flush_globals_to_heap`, `submit_lights`, `submit_tiled_lighting`, `submit_materials`,
  `get_globals_state`, `set_globals_state`) retarget from `s_recording_context` to the globals
  pointer. Their signatures and call sites (12× `submit_camera`, 4× `flush_globals_to_heap`
  across `renderer_3d_deferred.cpp:81`, `renderer_3d_ssao.cpp:155`, `renderer_3d_meshlet.cpp:103`,
  `debug_renderer_3d.cpp:165`, the `get/set_globals_state` save/restore stacks) are untouched.
- Add `submit_shadow_matrices` / `submit_directional_shadows` statics forwarding to
  `set_shadow_matrices` / `set_directional_shadows`, and retarget the two callers
  (`renderer_3d_shadow.cpp:94`, `:489`). This removes shadow code's need for its cached
  `res->vk_ctx` for uploads and gives all producers one uniform surface. The `frame` arguments
  those call sites compute (`:137`, `:455`) become unnecessary for the upload calls — the
  setters write host state only.
- Known pre-existing gap, do not fix silently: `GlobalsState` (`vk_renderer_api.h:76-89`) does
  not carry `materials` / `materials_ssbo_offset`, so `get/set_globals_state` save/restore drops
  them. Preserve that behavior in the move; flag it in the code with a comment.

**B5. `VulkanContext` deletions** — everything the table in B1 vacated, plus: the init block
(`vk_context.cpp:1059-1065`: layout build + both create calls), the `record_globals_upload` call
(`:1229`) and `m_pending_globals.reset()` (`:1243`) in `begin_frame_recording`, the snapshot
(`:1285-1293`) in `end_frame_recording`, the cleanup calls (`:2457-2458`) in `destroy`, and the
`flush_globals_to_heap` / `pending_globals` public API (`vk_context.h:127-130`).
`set_recording_context` (`:1105`) stays — it is recording-lifecycle mechanism, not globals.

Verify Part B: build renders identically (A/B screenshot); the fitness-check grep —
`grep -n "globals\|materials\|shadow\|camera\|lights" engine/src/platform/vulkan/vk_context.h`
— returns only font machinery and comments.

## Part C — Materials to device-local, inside `VulkanRendererGlobals`

The design carried over from the superseded plan, now confined to one class. Model on the
globals-buffer machinery that Part B just moved in.

> **Implemented, with C2/C3 redesigned.** The dirty-range scheme below **cannot be built as
> written**, and the reason generalises — record it before touching this area again.
>
> C3 has `begin_frame` record the materials copy from *"that frame slot's stored range"* saved by
> `snapshot`. But slot `f`'s stored range is from frame N−2, while frame N's snapshot overwrites
> that same staging buffer: recorded region and staged bytes disagree, and frame N's materials are
> silently truncated. The constraint underneath is a timing one —
>
> - the copy region is fixed when the copy is **recorded**: frame top, outside any render pass;
> - the materials only **exist** mid-frame, inside the g-buffer pass
>   (`frame_graph.cpp:332-344` wraps every graphics executor in `vkCmdBeginRenderPass`), where
>   `vkCmdCopyBuffer` is illegal and the consuming draws follow immediately.
>
> A dirty range is a *precise* description of what changed, so it can only be computed after the
> change — always too late for the record point. **A high-water mark is a bound**: monotonic,
> computable from history, available at frame top, guaranteed to cover everything live. Copying a
> superset is always correct; the surplus is a re-copy of identical bytes. So C2/C3 became:
> `m_materials_hwm` + a per-frame `m_materials_upload_elems[]` that keeps the staged byte count and
> the recorded region in lockstep, with `flush_pending` reduced to a pure host-side mirror write.
>
> This preserves the goal the dirty range was chosen for — cost tracks the live material count
> (~224 KB for a Sponza-sized draw list), never the 3.5 MB capacity — and it makes **R4 vacuous**:
> there is no dirty range left to mis-book. The one accepted cost is that a frame whose material
> count exceeds the session high (scene load, entity add) lands its new tail one frame late,
> reading the *default* material rather than garbage. The count is view-independent
> (`scene.cpp:1121` iterates every `MeshRendererComponent` with no culling), so this never fires on
> camera motion.
>
> Two alternatives were weighed and rejected **for now**: a dedicated per-frame upload command
> buffer submitted ahead of the main one (exact ranges, zero latency including growth — but it
> widens the change into the submit path, whose fence/image index-space mismatch is a known
> outstanding hazard), and an unconditional full-capacity copy (~440 MB/s each way, and its cost is
> tied to the `k_max_material_count` ceiling rather than to what the scene uses). The upload-CB
> design remains the natural upgrade: it changes only *where* the copy is recorded, leaving the
> buffer, binding, mirror and barriers as they are.
>
> Also landed: `k_max_material_count` moved from `VulkanContext` to `VulkanRendererGlobals`. It was
> the last renderer-concept name on the context, so the fitness-check grep in the invariants below
> now returns **zero** hits rather than "only font machinery".

**C1. The buffer.** One `m_materials_buffer`, **DEVICE_LOCAL**, usage
`STORAGE | TRANSFER_DST | SHADER_DEVICE_ADDRESS`, capacity
`sizeof(GPUMaterial) * VulkanContext::k_max_material_count` (~3 MB at the current 16384 cap;
`GPUMaterial` at `gpu_types.h:54`). Single buffer, not per-frame — staging + barriers make that
safe, and it removes the inherited hazard where a frame that skips `submit_materials` presents
a frame-slot buffer that is ~2 frames stale. Created in `VulkanRendererGlobals::init` alongside the
globals buffer.

**C2. Host copy + dirty range.** `std::vector<uint8_t> m_materials_cpu` (capacity-sized,
zero-initialized — a zeroed `GPUMaterial` is inert) plus a dirty range in **elements**
(`m_materials_dirty_lo/hi`). The materials block in `flush_pending` (the old
`flush_globals_to_heap` materials half) becomes: memcpy `p.materials` into `m_materials_cpu` at
`p.materials_ssbo_offset * sizeof(GPUMaterial)`, extend the dirty range, done — no mapping, no
binding write. Dirty tracking is the point: snapshotting a fixed ~3 MB every frame whether or
not materials changed is what makes the naive port wrong.

**C3. Staging + upload.** Per-frame staging buffers (`TRANSFER_SRC`, host-visible, persistently
mapped), capacity-sized — ~3 MB × frames-in-flight is an acceptable ceiling that avoids resize
logic. In `snapshot(frame)`: if the dirty range is non-empty, memcpy just that byte range into
the frame's staging buffer at the same offset, record the range into a per-frame `{lo, hi}`
slot, and clear the live dirty range. In `begin_frame(cmd, frame)`: after the globals copy,
record a `vkCmdCopyBuffer` of **that frame slot's** stored range, bracketed by the same barrier
pair the globals copy uses. The frame-slot indirection is load-bearing: recording from the live
`m_materials_dirty_*` instead of the frame's own snapshot re-creates a frames-in-flight race.

**C4. The binding, written once.** In `init`'s heap-registration loop (the old `:817-831`), the
`ExternalStorage` arm currently allocates a slot and writes nothing. Change it to write the slot
immediately with `m_materials_buffer`'s device address and full capacity — using
`k_global_bindings[(size_t)GlobalBinding::Materials].shader_binding`, fixing the hardcoded `2`.
Then delete `write_materials_heap_binding` and the per-frame host-visible materials buffers.
`VulkanDescriptorHeap::write_global_buffer` (`vk_descriptor_heap.cpp:569-573`) loses its only
caller — grep at implementation time, then delete it too.

**Considered and rejected — folding materials into the globals buffer** (making it
`GlobalBufferKind::Storage` with a real size): it would ride the existing snapshot for free, but
that snapshot is a full-buffer memcpy + GPU copy every frame — growing it by ~3 MB of
mostly-unchanged data is the wrong trade. `ExternalStorage` exists for exactly this case.

## Part D — Dissolve the hotfix

> **Done — absorbed by B and C, plus one leftover.** Part B deleted
> `create_global_buffer_resources` / `cleanup_global_buffer_resources` from `VulkanContext`
> outright, and Part C deleted the `m_materials_ssbo*` members along with
> `write_materials_heap_binding` and `VulkanDescriptorHeap::write_global_buffer` (R5 discharged:
> re-grepped at implementation time, zero callers). Both sweeps below pass — sweep 1 returns only
> the parked SDF font machinery (`m_band_table_ubo_memories`, `m_curve_ubo_memories`), sweep 2
> returns zero.
>
> One item the sweeps didn't cover, caught by hand: `vk_context.h` still included
> `Honey/renderer/gpu_types.h`. B5 said to keep it "only if something else in the header still
> needs it" — after B, nothing did, in either the header or the `.cpp`, for any of the 15 types
> that file defines. Removed. Nothing was relying on it transitively; every `vk_context.h`
> dependent rebuilt clean.
>
> Also noted, deliberately **not** changed: `Renderer::globals()` has zero callers. Producers reach
> the object through `VulkanRendererAPI`'s static instead. It's dead API today, but B specified it
> on purpose and the multi-client argument in the motivation above is what it's for — kept so the
> Renderer2D rewrite has the handle it will want. Delete it if that rewrite doesn't materialise.

Final sweeps:
- `grep -rn "ssbo_memories\|_ubo_memories" engine/src/platform/vulkan/` → only font machinery.
- `grep -rn "pending_globals\|flush_globals" engine/src/platform/vulkan/vk_context.*` → zero.

## Files Changed Summary

| File | Part | Change |
|------|------|--------|
| `vk_context.cpp` | A | Shadow uploads keep only `m_globals_cpu` writes (guard → assert) |
| `vk_context.h/.cpp` | A | Delete dead trios + two definition-less decls; shrink hotfix creator |
| `vk_renderer_globals.h/.cpp` (**new**) | B | `VulkanRendererGlobals` — all set-0 globals state + logic |
| `vk_context.h/.cpp` | B | Delete all globals members/methods + lifecycle call sites |
| `renderer.h/.cpp` | B | Own/create/destroy the object; `begin_frame` orchestration; new `end_frame()`; `globals()` accessor |
| `engine.cpp` | B | Call `Renderer::end_frame()` before `m_window->on_update()` |
| `vk_renderer_api.h/.cpp` | B | `set_globals`; retarget producers; add shadow submit statics |
| `renderer_3d_shadow.cpp` | B | Shadow uploads via `VulkanRendererAPI` instead of `res->vk_ctx` |
| engine CMake source list | B | Add `vk_renderer_globals.cpp` |
| `vk_renderer_globals.h/.cpp` | C | Device-local materials buffer + high-water staging; binding written once |
| `vk_descriptor_heap.h/.cpp` | C | Delete `write_global_buffer` (zero callers, re-verified) |
| `vk_context.h` | C | `k_max_material_count` moves to `VulkanRendererGlobals` |
| `vk_context.h` | D | Drop the now-unused `gpu_types.h` include |

## Implementation Order

**A → B → C → D**, committing each part. B is the risky one and is deliberately a verbatim move
so any rendering difference after it is by definition a porting mistake, not a design question.
No shader or pipeline-layout changes anywhere in this plan — binding slots, types, and the
synthesized `global_bindings.glsli` are untouched — so **no cache clearing is needed**; if a
step seems to require clearing `assets/cache/`, it changed something this plan says it
shouldn't.

## Verification Checklist

Run from `cmake-build-debug/` (content browser resolves `../assets`).

**After Part A:**
- [ ] Point + directional shadows identical; move a light and the camera.
- [ ] The A-sweep grep returns zero hits.

**After Part B:**
- [ ] A/B screenshot against pre-B: Sponza identical (materials, shadows, SSAO, deferred
      lighting).
- [ ] `DebugRenderer3D` collider gizmos correct in edit and play mode (it consumes camera
      globals through the moved path).
- [ ] Renderer-type switch Deferred → Forward → Path Tracer → back: no crash, no assert from
      `register_global_binding` (would indicate a lifecycle violation of the device-lifetime
      invariant).
- [ ] The `vk_context.h` fitness-check grep returns only font machinery.
- [ ] Validation layers clean.

**After Part C (the real checks):**
- [ ] Sponza materials correct — the regression scene is the regression test.
- [ ] **Live-edit a material** in the inspector (albedo, metallic, roughness) — changes appear
      next frame. This exercises the dirty-range path; a static scene can never catch a
      bookkeeping bug here.
- [ ] Load a second scene and return — `materials_ssbo_offset` appends land at the right
      elements, no index shift.
- [ ] Renderer debug panel heap dump: Materials slot written once, address stable across frames.
- [ ] Sync validation (`VK_LAYER_KHRONOS_validation` synchronization checks) enabled for at
      least one run: no hazards on the materials copy.
- [ ] Path tracer unaffected (it has its own material plumbing).

**After Part D:**
- [ ] Full session (load, edit, play, save, quit) clean; no descriptor or memory warnings at
      shutdown.

## Risks

**R1 — Snapshot pairing is no longer structural.** Today the snapshot lives inside
`end_frame_recording`, so it cannot be skipped. After B it depends on `Application::run` calling
`Renderer::end_frame()` before `m_window->on_update()`. A frame submitted without it uses the
*previous* frame's staging content — globals one frame stale, visually subtle (camera lag under
motion). If gizmos or shadows seem to trail the camera by a frame after B, check this pairing
first. Mitigation: `snapshot()` records the frame counter it last ran for; `begin_frame()`
asserts the previous frame was snapshotted.

**R2 — Barrier scope on shared buffers.** Frame N's transfer writes must wait on frame N-1's
shader reads. The moved globals barrier already solves this — Part C's materials copy must reuse
those exact stages and access masks, not re-derive them; divergence produces intermittent
corruption only under GPU load.

**R3 — Init-order regressions.** Two hard orderings: globals init before `Renderer3D::init`
(descriptor-mapping assert fires at first pipeline creation if violated — loud, good), and
destruction after `wait_idle` but before context/backend teardown (violation = validation
errors at shutdown, or a crash on device-lost handles).

**R4 — Dirty-range bookkeeping.** Fails only on partial updates; the live-edit check is
mandatory, not optional. The per-frame `{lo, hi}` slot (C3) is the specific trap: the copy
recorded at frame N's top must use frame N's snapshot range, never the live dirty range.

**R5 — Deleting `write_global_buffer`.** Heap API surface; something could adopt it between
planning and implementation. Grep at implementation time, not from this doc's claim.
