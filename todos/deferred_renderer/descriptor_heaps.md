# Descriptor Heaps + Reflection + Frame-Graph Automation

*A migration tutorial for the Vulkan backend.*

**Steps 0–8 are done.** SSAO, deferred lighting, and the meshlet path are fully migrated onto
`VK_EXT_descriptor_heap` — reflection, the `VulkanDescriptorHeap` subsystem, static samplers,
heap-mode pipelines, frame-graph descriptor automation, and all three pass migrations are landed
and confirmed in code (`Pipeline::create_heap_mode` is used by both `renderer_3d_ssao.cpp` and
`renderer_3d_deferred.cpp`; the old hand-rolled `ssao_set_layout`/pool/sets plumbing is gone).
Correctness bugs and hazards found during that migration were tracked in `descriptor_heap_fixes.md`
(dead sampler-heap code, inert/misleading push-constant fields, a hardcoded global-slots array
size, and heap capacity sizing) — that doc is now fully closed and has been deleted; see git
history if you need it.

**Steps 9 and 10 are now both closed too.** Step 9 closed via a follow-up audit — the remaining
"legacy" descriptor code turned out to be load-bearing for systems intentionally staying on the
classic path, not cruft; see that section for the corrected facts. Step 10's remaining items
(profiler annotations, heap dump) landed: `HN_GPU_SCOPE(m_cmd, "FG: Heap Bind")` wraps the heap
bind + descriptor writes in `FrameGraphPassContext::bind_heap_pipeline`
(`frame_graph.cpp`), and `VulkanDescriptorHeap` now tracks an always-on per-frame `DumpEntry` list
(offset/size/type/resource/pass), surfaced in a new "Descriptor Heap" section of
`RendererDebugPanel` — confirmed working in the editor. The original Steps 0–8 walkthrough has
been trimmed from this file since it's historical at this point — see git history if you need it.

---

## What we're building, and why

Right now every renderer component hand-rolls its own descriptor pipeline:
a `VkDescriptorSetLayout`, a `VkDescriptorPool`, a per-frame `VkDescriptorSet`,
and `vkUpdateDescriptorSets` calls, usually parked in a file-static singleton.
SSAO does it, the deferred renderer does it, the meshlet path does it. It's a
lot of duplicated lifetime-management code, and every new pass repeats it.

We're replacing all of that with three sources of truth working together:

- **the shader** decides *what bindings exist* — we learn them by reflecting SPIR-V,
- **the frame graph** decides *which resources fill those bindings* — it already
  knows each pass's reads and writes,
- **`VK_EXT_descriptor_heap`** (Vulkan 1.4.340, Jan 23 2026) is the GPU-side
  mechanism that lets us write descriptors as plain bytes into a big buffer and
  index them from the shader.

**The end state.** A pass executor like `Renderer3DSSAO::execute_draw` shrinks to:
bind pipeline, draw. No layouts, no pools, no sets, no `vkUpdateDescriptorSets`.
By the time the executor runs, the frame-graph harness has already written every
descriptor the pass needs into the resource heap and pushed the base offset with
`vkCmdPushDataEXT`.

This is a learning engine, so we deliberately take the "future Vulkan looks like
this" path rather than the "ship to the widest install base" path. We commit to
descriptor-heap-only and delete the legacy descriptor-set code once the new path
reaches parity.

## How this document is organized

Ten steps, meant to be done roughly in order, each a natural commit boundary:

| Step | What you build | Lands as |
| ---- | -------------- | -------- |
| 0 | Preflight checks + shader-model decision | (no code) |
| 1 | SPIR-V reflection of descriptor bindings | standalone, no behavior change |
| 2 | The `VulkanDescriptorHeap` subsystem | built + self-tested, no renderer uses it |
| 3 | Static samplers baked into the sampler heap | extends Step 2 |
| 4 | Heap-mode pipelines | opt-in flag, old path still default |
| 5 | Frame-graph descriptor automation | opt-in per pass |
| 6 | Migrate SSAO | first real user — debug here |
| 7 | Migrate the deferred renderer | the main payoff |
| 8 | Migrate the meshlet path | covers persistent (non-transient) allocations |
| 9 | Delete legacy descriptor code | only after 6–8 are solid |
| 10 | Debug aids | start during Step 5, grow through 7 |

Steps 1–4 are infrastructure. The first user-visible win is Step 5.

## Step 9 — Tear out the legacy descriptor code

**Done — closed by audit, not by further deletion.** `VulkanRendererAPI::submit_set1_descriptor_set`,
`get_or_create_meshlet_set_layout`, `ensure_mesh_descriptor_set`, the `s_meshlet_desc_pools`
machinery, and `destroy_meshlet_descriptors` are gone — confirmed no matches left in
`vk_renderer_api.cpp`. A follow-up audit resolved the remaining open items — all three turned out
to be legitimately load-bearing exceptions, not leftover cruft, so nothing further is deletable
under the "`renderer_3d` is heap-only" goal without expanding scope to a whole-engine legacy-path
migration:

- The `extra_set_layout` overload of `Pipeline::create`/`create_mesh` (`vk_pipeline.h/.cpp`) is
  used at two real (non-null) call sites: `renderer_2d.cpp:270,864`, wiring up the engine's own SDF
  glyph/icon text renderer (`Renderer2D_Text.glsl`) to `VulkanContext`'s font descriptor set. The
  shadow module's classic pipeline also uses the overload (`renderer_3d_shadow.cpp:243,665`), but
  passes `nullptr` for `extra_set_layout` — it isn't actually exercising the extra-set feature, so
  removing the overload would still break `renderer_2d`'s text path. **Correction to the framing
  above:** the shadow classic pipeline is *not* a non-mesh-shader-hardware fallback. Shadow
  rendering (`Renderer3DShadow::init`) only ever initializes when `supports_mesh_shader()` is true
  and hard-asserts on `vkCmdDrawMeshTasksEXT` (`renderer_3d_shadow.cpp:109-112`,
  `scene_viewport_renderer.cpp:84-87`) — the classic/meshlet split is a runtime debug toggle (the
  `GeometryPath` combo box, `renderer_debug_panel.cpp:172-178`) for side-by-side comparison, not a
  capability fallback. On hardware without mesh shaders, shadows are silently disabled regardless
  of which geometry path is selected — a latent gap, out of scope here, but noted so it isn't
  rediscovered as a mystery later.
- `VulkanContext` still carries `m_global_set_layout` / `m_descriptor_pool` /
  `m_global_descriptor_sets`, read by the shadow classic path via `get_global_descriptor_set()`
  (`renderer_3d_shadow.cpp:299,715`), but the underlying storage is populated and consumed far more
  broadly by `VulkanContext::apply_pending_globals` (`vk_context.cpp:2060-2069`, called from
  `vk_renderer_api.cpp:314`) for Renderer2D, the forward pass, classic G-buffer geometry, and the
  debug renderer — all still legitimately on the classic path. Not deletable without migrating that
  whole legacy 2D/forward path, which is out of scope.
- **Correction:** `m_font_set_layout` / `m_font_descriptor_pool` / `m_fonts_descriptor_sets` are
  *not* ImGui's, despite the original framing here. ImGui's Vulkan backend manages its own
  descriptors entirely separately, via `ImGui_ImplVulkan_Init` and
  `VulkanBackend::get_imgui_descriptor_pool()` (`imgui_layer.cpp:93-127`, `vk_backend.cpp:806`) —
  that part of the original guidance ("leave ImGui alone, we don't get to choose") still holds, it
  was just attached to the wrong members. The font-descriptor members instead back the engine's own
  text renderer (see `extra_set_layout` bullet above) and are genuinely load-bearing.
- **Confirmed via audit — cloth sim and path tracer are cleanly isolated, no action needed.** Cloth
  sim (`vk_cloth_sim.h/.cpp`, `vk_cloth_renderer.h/.cpp`) and the path tracer
  (`renderer_3d_pathtracer.cpp`) each own a fully private `VkDescriptorSetLayout`/`VkDescriptorPool`
  and never reference any `VulkanContext` global-descriptor symbol or the `extra_set_layout`
  overload. Matches the original "leave alone" guidance exactly. The target here is "renderer_3d is
  heap-only", not whole-engine purity, and by that measure Step 9 is complete.

---

## Step 10 — Debug aids

We're giving up driver validation for descriptor correctness, so build our own
safety net.

1. ~~**CPU shadow table.**~~ Done — `m_shadow_table` in `vk_descriptor_heap.h/.cpp`.
2. ~~**Profiler annotations.**~~ Done — `HN_GPU_SCOPE(m_cmd, "FG: Heap Bind")` in
   `FrameGraphPassContext::bind_heap_pipeline` (`frame_graph.cpp`) wraps `heap->bind`,
   `push_pass_data`, and the per-pass descriptor-write loop. One fixed zone name aggregates cost
   across every pass that goes through heap-mode binding each frame, matching
   `HN_GPU_SCOPE`'s per-callsite-static design (it can't take a dynamic name at a single call site).
3. ~~**Heap dump.**~~ Done. `VulkanDescriptorHeap` now records an always-on (not
   `HN_ENABLE_ASSERTS`-gated — this is informational, not a correctness check like
   `m_shadow_table`) per-frame `DumpEntry{offset, size, type, resource_name, pass_name}` list,
   populated from `write_buffer`/`write_image`'s new optional `pass_name` parameter (only
   `bind_heap_pipeline`'s two call sites pass one; persistent/global/bindless writes aren't
   pass-scoped and are left out) and cleared each `begin_frame`. `RendererDebugPanel` reads it via
   `get_frame_dump_entries()` and renders offset/size/type/resource/pass as a table in a new
   "Descriptor Heap" section — confirmed showing live data in the editor.

---

## What's left, prioritized

Steps 0–10 are closed. What remains is smaller, decoupled work — some of it inherited "open
questions," some of it new since the Step 9 audit surfaced exactly which classic-descriptor-set
consumers are still alive and why. Ordered highest-value first.

### Worth doing

1. **Billboard icons for lights/cameras in the scene viewport, built heap-mode-native.**
   Tracked as a feature ask in `todos/short_todos.md` ("2D overlays for lights and cameras in the
   scene viewport") — non-mesh entities are invisible in the viewport today, which hurts scene
   editing usability. Investigated whether this should reuse the existing `Renderer2D` SDF
   text/icon renderer (`renderer_2d.cpp`, already draws `IconRendererComponent` icons in world
   space via `draw_icon`, `renderer_2d.cpp:1628-1714`) and migrate *that* to heap mode. Conclusion:
   **build a small net-new heap-mode pass instead**, for two independent reasons:
   - `Renderer2D`'s quad orientation (`decompose_transform`, `renderer_2d.cpp:1266-1281`) only
     extracts position/scale/Z-rotation — there's no camera-facing billboard math anywhere in it.
     That gap has to be built regardless of which renderer hosts it, so migrating the existing
     renderer doesn't save that work.
   - The SDF vector-icon/font machinery (`SlugIcon`/`SlugFont`, comparison-signed-distance curves)
     solves a problem billboard gizmo icons don't have — they're small, fixed-size, and fine as a
     plain textured quad. Reusing it would mean dragging heap-mode migration through unrelated
     complexity (two SSBOs, `VulkanContext`'s font descriptor set, `extra_set_layout`) for no
     payoff.
   - A dedicated pass registers naturally in the frame graph next to SSAO/deferred (heap-mode from
     day one, no migration debt to repay later), and can carry an entity-id output for viewport
     picking the same way the mesh passes do — something bolting onto `EditorLayer::on_overlay_render`
     (`editor_layer.cpp:609`, today's home for physics-collider gizmos) wouldn't get for free.
   Effort: small — a handful of camera-facing quads, a tiny icon-atlas texture, `Pipeline::create_heap_mode`,
   no new descriptor-set machinery to design since it's greenfield.
2. **Sampler reflection.** spirv_cross can report that a sampler is a comparison sampler
   (`SamplerComparison`). Use that to auto-default those bindings to `ShadowCmp` without a YAML
   annotation. (Comparison-sampler selection today goes by the `"shadow"/"cmp"` name convention
   instead — works, but is a naming convention standing in for information reflection already has.)
   Small, self-contained, no dependencies on the item above.
3. **View disambiguation.** Ideally the shader's type expresses cube vs. 2D-array
   (`textureCubeArray` vs. `texture2DArray`) and reflection picks the right framebuffer accessor —
   which would remove the YAML `view_kind:` field entirely. Verify spirv_cross reports the image
   dimension cleanly; if it does, prefer that over the YAML field. Small, cosmetic (removes a
   manual annotation), no urgency.

### Not worth it

- **Migrating `Renderer2D`'s SDF text/icon renderer to heap mode.** No driving need once billboard
  icons get their own pass (see above) — that was the concrete use case that made this worth
  reconsidering. What's left is a general 2D text/icon renderer used for on-screen debug text and
  editor overlays; its descriptor footprint is already small and self-contained (two SSBOs behind
  `VulkanContext`'s font descriptor set, no textures), so leaving it on the classic path costs
  nothing. `bind_heap_pipeline` also assumes a registered frame-graph pass executor
  (`frame_graph.cpp:795-830`), not an immediate-mode call site like `on_overlay_render` — heap-mode
  migration here would mean restructuring the call pattern, not just swapping a descriptor set.
  Revisit only if `renderer_2d.cpp`'s broader "needs a major refactor" item in `short_todos.md`
  ever gets scheduled.
- **Migrating `VulkanContext`'s global descriptor sets to heap mode** (`m_global_set_layout` /
  `m_descriptor_pool` / `m_global_descriptor_sets`, populated by `apply_pending_globals`,
  `vk_context.cpp:2060-2069`). This is what the general-purpose `Renderer2D` path, the forward
  pass, classic G-buffer geometry, and the debug renderer all still run on. Migrating it is a
  whole-engine legacy-path project, not a contained pass migration like SSAO/deferred/meshlet
  were — and part of what it serves (the forward path) is itself flagged low-priority in
  `short_todos.md` ("Use `TiledLighting` in the Forward renderer") pending confirmation it's still
  needed. Don't start this without first deciding the forward path's fate.
- **Migrating the shadow module's classic pipeline to heap mode.** Per the Step 9 audit, this
  isn't a hardware-capability fallback — it's a runtime debug toggle (`GeometryPath` combo,
  `renderer_debug_panel.cpp:172-178`) that exists specifically to give a side-by-side comparison
  against the heap-mode meshlet shadow path. Migrating it to heap mode would either eliminate the
  thing it's for (nothing left to compare against) or require keeping two heap-mode
  implementations of the same pass around, which is worse than the current one-classic/one-heap
  split. Leave it as the intentional exception it is.