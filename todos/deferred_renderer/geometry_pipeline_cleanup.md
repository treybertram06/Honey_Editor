# Heap Globals Completion: The C5 Buffer Regression and Finishing the Migration

> **The migration plan in this document (Parts A–C) is superseded by
> [`renderer_globals_migration.md`](renderer_globals_migration.md).** The ownership discussion
> that followed this draft rejected its position that materials/shadow globals may stay on
> `VulkanContext` — they move to a new `RendererGlobals` object owned by the high-level
> `Renderer`. The incident post-mortem below ("The incident", "Current state") remains the
> authoritative record of the C5 regression.

## What this document now covers

The geometry-pipeline cleanup this file previously planned (meshlet-only geometry, Renderer2D
compiled out, classic descriptor machinery deleted — Parts A/B/C) **shipped**, through commit
`8bb3f15` plus the Part-C5 working tree. Immediately after C5, Sponza rendered as shiny black
metal and shadows silently died. This document explains that regression, records the hotfix that
is currently in the tree, and plans the proper fix: moving the last classic-era buffers to a
genuine heap-mode design so the hotfix scaffolding can be deleted.

---

## The incident

**Symptom:** every material rendered with black albedo but correct specular highlights; light
positions and colors were right. Point and directional shadows were also dead, though that was
invisible in an unlit-looking scene.

**Root cause:** `create_global_descriptor_resources()` was dual-purpose, and the C5 plan bullet
treated it as classic-only. It created the classic descriptor set layout / pool / per-frame sets
(genuinely dead), but it was **also the only creator of the per-frame host-visible buffers
themselves** — and three of those buffers were load-bearing for the *heap* path:

1. **Materials SSBO.** Heap-mode shaders read materials through global binding 2
   (`GlobalBinding::Materials`, kind `ExternalStorage` in `global_bindings.h:32`), which
   `write_materials_heap_binding` (`vk_context.cpp:934`) points at `m_materials_ssbo[frame]`'s
   device address, and `flush_globals_to_heap` (`vk_context.cpp:1693`) fills by mapping
   `m_materials_ssbo_memories[frame]`. With the buffers never created, both sites hit their null
   guards and **silently no-op'd**: binding 2 was never written, shaders read zeroed materials —
   black albedo, dielectric-default specular. Exactly the symptom.
2. **Shadow-matrix SSBOs (point + directional).** `upload_shadow_matrices` and
   `upload_directional_shadows` (`vk_context.cpp:946`, `:967`) early-return on the null classic
   buffer **before** reaching their `m_globals_cpu` mirror writes — the memcpys heap-mode lighting
   actually depends on. Shadow data never reached the globals buffer.

Camera, Lights, and TiledLighting were unaffected because `flush_globals_to_heap` writes them
straight into `m_globals_cpu` — which is why lights and specular looked correct and narrowed the
diagnosis to the two paths above.

**Why it was silent:** every failing site was guarded by an `if (buffer) ... return;`-style null
check meant for init-order safety. Nothing asserted, nothing logged. A missing load-bearing
resource degraded to "render wrong forever."

**The hotfix (currently in the tree):** `create_global_buffer_resources()` /
`cleanup_global_buffer_resources()` (`vk_context.cpp:402`, `:465`; decls `vk_context.h:222-223`;
calls `vk_context.cpp:1064`, `:2457`) — a trimmed restoration of just the three still-referenced
buffer sets (materials, shadow matrices, directional shadow), with the descriptor-set machinery
left deleted. Verified working: materials and both shadow types render correctly.

**Lesson recorded:** this is the failure mode the old doc's own preamble warned about — a
sub-plan bullet ("delete the create/cleanup pair, `cpp:532-696`") drifting past a buried
dependency. The classic and heap paths *shared ownership* of these buffers, and nothing in the
code's structure said so. The proper fix below exists precisely to make ownership explicit so
this class of deletion is safe next time.

## Current state (what the hotfix leaves imperfect)

- **Materials** live in per-frame **host-visible** SSBOs — the buffers the GPU samples from
  directly. Every other global moved to the device-local `m_globals_buffer` with staged uploads
  (commit `fda37b8`) exactly because host-visible-read-by-GPU was the source of the shadow-wiggle
  bug. Materials never got that treatment.
- The materials heap slot is **rewritten every upload**: `flush_globals_to_heap` calls
  `write_materials_heap_binding(frame)` (`vk_context.cpp:1699`) each time producers submit,
  because the bound address flips between the per-frame buffers. The slot was allocated as a
  persistent resource at heap bring-up (`vk_context.cpp:818-824`, the `ExternalStorage` arm) but
  is never persistent in practice.
- The shadow upload functions **double-write**: the classic-buffer `vkMapMemory` halves write
  buffers that nothing reads anymore; only the `m_globals_cpu` mirror memcpys are live.
- `vk_context.h:346-360` still declares fully dead members: `m_camera_ubos`, `m_lights_ubos`,
  `m_tiled_lighting_ssbos` (+ memories + sizes) have **zero** references outside the header.
- Cross-frame staleness hazard (pre-existing, inherited): with per-frame materials buffers, a
  frame in which producers skip `submit_materials` leaves that frame-slot's buffer holding
  whatever it had ~2 frames ago.

## Invariants (hold across every step)

Inherited from the parent cleanup plan and still binding:

- **`VulkanContext` stays renderer-agnostic.** Materials/shadow globals are part of the set-0
  globals contract `VulkanContext` already owns (`m_globals_buffer`, `GlobalsLayout`,
  `k_global_bindings`) — keeping them here is consistent. Nothing renderer-named (SSAO, meshlet,
  g-buffer, …) may land on it as part of this work.
- **Heap mode is the only descriptor model.** No step may reintroduce per-draw or per-frame
  descriptor-set allocation.

New for this migration:

- **`k_global_bindings` (`global_bindings.h`) stays the single source of truth for set 0.** Any
  change to how materials are bound goes through the table and the heap bring-up loop that
  consumes it — no side-channel binding writes.
- **After Part B, the materials heap slot is written once, at creation, and its address never
  changes.** A per-frame binding rewrite reappearing is the smell that the migration regressed.
- **No GPU-visible buffer is host-written while the GPU may read it.** Device-local + per-frame
  staging + barrier, per the `m_globals_buffer` pattern — not host-visible memory read directly
  by shaders.
- **No silent degradation for load-bearing resources.** Where this plan deletes an init-order
  null guard, it is replaced by an assert, not by nothing.

---

## Part A — Delete the dead classic halves

Pure deletion; no behavior change. Independently committable.

**A1. Shadow uploads** — `vk_context.cpp:946-988`
In both `upload_shadow_matrices` and `upload_directional_shadows`: delete the early-return guard
and the `vkMapMemory`/`memcpy`/`vkUnmapMemory` half (the classic descriptor set was the only
reader). The `m_globals_cpu` mirror memcpy becomes the whole body; replace its
`if (!m_globals_cpu.empty())` with `HN_CORE_ASSERT` — after heap bring-up it must never be empty,
and a silent skip here is exactly how the incident stayed hidden.

**A2. Hotfix creation shrinks** — `vk_context.cpp:402-497`
Remove the `shadow_matrices` and `dir_shadow_matrices` entries from
`create_global_buffer_resources` / `cleanup_global_buffer_resources`. After A2 the pair manages
only the materials buffers (and dissolves entirely in Part C).

**A3. Dead members** — `vk_context.h:338-368`
Delete `m_camera_ubos`/`m_camera_ubo_memories`/`m_camera_ubo_size`, the `m_lights_ubos` trio, the
`m_tiled_lighting_ssbos` trio, and (after A1/A2) the `m_shadow_matrices_ssbos` and
`m_dir_shadow_ssbos` trios. Verify with
`grep -n "m_camera_ubo\|m_lights_ubo\|m_tiled_lighting_ssbo\|m_shadow_matrices_ssbo\|m_dir_shadow_ssbo" engine/src -r`
→ zero hits.

## Part B — Materials to a device-local, persistently-bound buffer

Model everything on the existing `m_globals_buffer` machinery: creation
`vk_context.cpp:738-833`, upload recording `record_globals_upload` (`:835`), host snapshot in
`end_frame_recording` (`:1290-1294`).

**B1. The buffer.** One `m_materials_buffer` (VkBuffer + VkDeviceMemory), **DEVICE_LOCAL**,
usage `STORAGE | TRANSFER_DST | SHADER_DEVICE_ADDRESS`, capacity
`sizeof(GPUMaterial) * k_max_material_count` (~3 MB at the current 16384 cap; `GPUMaterial` is
`gpu_types.h:54`). Single buffer, not per-frame — the staging/barrier pattern makes that safe,
and it eliminates the cross-frame staleness hazard noted above. Create it inside
`create_global_descriptor_heap_resources`, next to `m_globals_buffer` — that guarantees it exists
before the binding write in B4 and lets Part C delete the hotfix function outright.

**B2. Host-authoritative copy + dirty range.** Add `std::vector<uint8_t> m_materials_cpu`
(capacity-sized, zero-initialized like `m_globals_cpu` — a zero `GPUMaterial` is inert) and a
dirty range in **elements**: `m_materials_dirty_lo/hi`. The materials block in
`flush_globals_to_heap` (`vk_context.cpp:1693-1700`) becomes: memcpy `p.materials` into
`m_materials_cpu` at `p.materials_ssbo_offset * sizeof(GPUMaterial)`, extend the dirty range,
done — no mapping, no binding write. Dirty tracking matters here in a way it doesn't for
`m_globals_cpu`: snapshotting a fixed ~3 MB every frame whether or not materials changed is the
cost that makes the naive approach wrong.

**B3. Staging + upload.** Per-frame staging buffers (`TRANSFER_SRC`, host-visible, persistently
mapped) sized to full capacity — ~3 MB × frames-in-flight is an acceptable ceiling and avoids
resize logic. In `end_frame_recording`, next to the globals snapshot (`:1290`): if the dirty
range is non-empty, memcpy just that byte range into the current frame's staging buffer (same
offset), and record the range in a small per-frame `{lo, hi}` so the copy command knows what to
transfer. In `record_globals_upload` (extend it; it is already the "top-of-frame globals upload"
hook wired into command-buffer begin): record a `vkCmdCopyBuffer` of the dirty region
staging → `m_materials_buffer`, bracketed by the **same barrier scheme the globals copy already
uses** (wait for prior-frame shader reads before the transfer write; make the write visible to
this frame's reads). Clear the dirty range only after it has been staged for the current frame.
One subtlety the globals path doesn't have: the copy is range-based, so the per-frame `{lo, hi}`
must ride with the frame slot — the range recorded at frame N's top belongs to frame N's
snapshot, not to whatever `m_materials_dirty_*` says by then.

**B4. The binding, written once.** In the heap bring-up loop's `ExternalStorage` arm
(`vk_context.cpp:818-824`): after allocating the persistent slot, immediately
`heap->write_buffer(slot, 0, materials_device_address, capacity, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)`
and register it — identical to what the non-external arm does with `base + offset`. Then delete
`write_materials_heap_binding` (`vk_context.h:230`, `cpp:934-944`) and its call at `:1699`.
`VulkanDescriptorHeap::write_global_buffer` (`vk_descriptor_heap.cpp:569`) loses its only caller
— verify with a repo-wide grep, then delete it too.

**Considered and rejected — folding materials into `m_globals_buffer` itself** (i.e., making it
`GlobalBufferKind::Storage` with a real size in `k_global_bindings`): it would ride the existing
snapshot/copy for free, but that snapshot is a *full-buffer* memcpy + GPU copy every frame —
growing it by ~3 MB/frame of mostly-unchanged data is the wrong trade. The `ExternalStorage` kind
exists for exactly this case; materials stay a dedicated buffer with range-based uploads.

## Part C — Dissolve the hotfix scaffolding

After B, `create_global_buffer_resources` / `cleanup_global_buffer_resources` manage nothing.
Delete the pair (`vk_context.cpp:402-497`, decls + comment `vk_context.h:216-223`), their calls
(`cpp:1064`, `:2457`), and the last classic members: `m_materials_ssbo`,
`m_materials_ssbo_memories`, `m_materials_ssbo_size` (`vk_context.h:354-356`). Final sweep:
`grep -rn "ssbo_memories\|_ubo_memories" engine/src/platform/vulkan/` → only hits should be the
new materials staging (if named that way) and font machinery (kept, pending the Renderer2D
rewrite).

## Files Changed Summary

| File | Part | Change |
|------|------|--------|
| `vk_context.cpp` | A | Shadow uploads keep only the `m_globals_cpu` writes (guard → assert) |
| `vk_context.h/.cpp` | A | Delete dead camera/lights/tiled-lighting/shadow buffer members + creation |
| `vk_context.h/.cpp` | B | `m_materials_buffer` (device-local) + `m_materials_cpu` + dirty range + per-frame staging; extend `record_globals_upload` and `end_frame_recording` |
| `vk_context.cpp` | B | `ExternalStorage` arm writes the materials binding at creation; delete `write_materials_heap_binding` |
| `vk_descriptor_heap.h/.cpp` | B | Delete `write_global_buffer` (verify zero callers first) |
| `vk_context.h/.cpp` | C | Delete `create/cleanup_global_buffer_resources` + last classic members |

## Implementation Order

**A → B → C.** A and B are independently committable; C is a trivial follow-up to B. No shader
or pipeline-layout changes anywhere in this plan — binding 2 keeps its slot, type, and shader
interface — so **no cache clearing is needed**, which is itself a useful property to verify (if a
step seems to require clearing `assets/cache/`, it changed something this plan says it
shouldn't).

## Verification Checklist

Run from `cmake-build-debug/` (content browser resolves `../assets`).

**After Part A:**
- [ ] Point + directional shadows identical to pre-change; move a light and the camera.
- [ ] The A3 grep returns zero hits.

**After Part B (the real checks):**
- [ ] Sponza materials correct — the regression scene is the regression test.
- [ ] **Live-edit a material** in the inspector (albedo, metallic, roughness) — updates appear
      the next frame. This exercises the dirty-range path; a stale material here means B2/B3
      range bookkeeping is wrong.
- [ ] Load a second scene, then return — offset-append uploads (`materials_ssbo_offset`) land at
      the right elements; no material index shift.
- [ ] Shadows + SSAO + path tracer unaffected (path tracer has its own material plumbing; it
      should not notice).
- [ ] Renderer debug panel heap dump: Materials slot written once, address stable across frames.
- [ ] Validation layers clean; no barrier hazards reported with sync validation enabled
      (`VK_LAYER_KHRONOS_validation` synchronization checks — worth switching on for this one).
- [ ] Descriptor-heap high-water unchanged (no new transient allocations appeared).

**After Part C:**
- [ ] Full editor session (load, edit, play mode, save, quit) clean; no descriptor or memory
      warnings at shutdown.

## Risks

**R1 — Dirty-range bookkeeping.** The failure mode is *stale materials only on partial updates*,
which a static scene will never show. The live-edit check above is mandatory, not optional.
Special care: the per-frame `{lo, hi}` snapshot in B3 — using the live `m_materials_dirty_*` at
copy-record time instead of the frame's own snapshot re-creates a frames-in-flight race.

**R2 — Barrier scope on a single shared buffer.** Frame N's transfer write must wait on frame
N-1's fragment-shader reads. The globals copy already solves this — copy its barrier stages and
access masks exactly rather than reasoning them out fresh; divergence here produces intermittent
corruption only under GPU load.

**R3 — Deleting `write_global_buffer`.** It is heap API surface; something outside `vk_context`
could adopt it between now and implementation. Grep at implementation time, not from this doc's
claim.

**R4 — Silent-guard removal.** A1 turns guards into asserts. If an assert fires during editor
bring-up, the fix is init ordering (create buffers before the first producer runs), never
reinstating the silent return — that guard is what turned a one-line bug into a debugging
session.