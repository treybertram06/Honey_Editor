# GPU Resource & Memory System: Allocation Classes and a Per-Frame Transient Ring

## Motivation

The goal driving all of this: **make renderer experimentation cheap**. Adding a module today (SSAO, vector icons, a new post effect) means touching five or more places, hand-rolling per-frame buffer lifetime, and hand-writing descriptor heap arithmetic. The immediate trigger was a frame-graph diagnostic on the vector icon module:

```
bind_heap_pipeline: pass 'VectorDraw' shader samples 'IconInstances' (set 1, binding 2)
but no declared read binding matches; descriptor will be undefined
```

That warning is legitimate. `IconInstances` is a per-frame CPU-built SSBO that the frame graph has no way to model, so the module hand-writes the descriptor at `renderer_3d_vector_icon.cpp:124-133` and hand-manages frames-in-flight via `icon_instance_buffers[frame_slot]`.

The conclusion from the design discussion: this class of resource **shouldn't be in the frame graph at all**. Render graphs exist to derive barriers from a declared read/write DAG and to alias transient GPU memory. A per-frame, CPU-built instance buffer has neither problem — no producer pass to synchronize against, nothing worth aliasing. What it needs is frames-in-flight safety and descriptor plumbing, and those belong to an allocator the engine doesn't currently have. Frostbite, Unreal RDG, id Tech and Granite all solve this the same way: a per-frame linear allocator sitting *below* the graph.

### Three tracks identified

| # | Track | Status |
|---|---|---|
| 1 | **Resolved-source shader cache hashing** | Agreed, small, prerequisite for track 3 |
| 2 | **GPU memory system** (this document) | **Decided — plan below** |
| 3 | **Struct codegen / shader resource import** | **Undecided — options recorded below** |

**Track 1 — shader cache hashing.** `ShaderCache::get_spirv_cache_path` hashes `target_tag + read_text_file(shader_path)` (`shader_cache.cpp:390-403`) — the top-level file only. Neither `#include`d files nor the synthesized `global_bindings` preamble are in the key. Editing a `.glsli`, or changing a `shader_binding` in `global_bindings.h`, does not invalidate cached SPIR-V today; you're insulated only by `kShaderCacheVersion` bumps. This is a latent bug now and a guaranteed correctness failure the moment struct layouts are generated into shaders. Fix: hash the fully-resolved source (all includes plus synthesized preamble). Roughly an afternoon.

**Track 3 — struct codegen (open).** Problem: `GPUIconInstance` is written twice, in `gpu_types.h:178` and in `Renderer3D_VectorIcon.glsl:35-47`, guarded only by `static_assert(sizeof(...) == 96)`. Size asserts don't catch field reordering or padding differences. Hard constraint: C++ has no reflection that yields field names — the options are self-describing macros, an external parser, or C++26 static reflection (not in any shipping compiler). Options discussed, no decision:

- **Macro-declared structs** — `HN_GPU_STRUCT(...)` emits C++ struct + field table + GLSL. Single source, ugly header, worse IDE navigation.
- **Loose generator + strict validator** *(leaning)* — keep `gpu_types.h` plain inside a marked region; a small parser emits the GLSL and a header of `static_assert(offsetof(...) == N)`. The compiler validates the generator's C++ model; SPIR-V reflection validates the std430 model. The generator is allowed to be naive because it isn't the safety guarantee.
- **`HN_GPU_BUFFER({...})` create-info declaration** — regardless of which struct approach wins, one declaration should drive the GLSL block, the descriptor binding, *and* the allocation class from track 2. This is the piece that ties tracks 2 and 3 together, and it's why track 3 should land after track 2 defines the classes.

Independently worth doing whenever: **SPIR-V reflection layout validation**. SPIRV-Cross is already linked (`shader_compiler.cpp`, `spvc_context`). Reflect each block's member offsets, compare against the C++ side, error with the offending field name. ~50 lines, kills the entire C++/GLSL drift bug class, and it protects what exists today regardless of which authoring approach is chosen.

---

# Track 2: GPU Memory System

## Invariants (hold across every step)

1. **`VulkanContext` stays renderer-agnostic.** The transient ring and persistent allocator are renderer concepts and live on `VulkanBackend`, never as `VulkanContext` members. `VulkanContext::begin_frame_recording` may *call into* them — exactly as it already does for the descriptor heap at `vk_context.cpp:611`.
2. **No module code contains `frame_slot` or `% k_max_frames_in_flight` after migration.** The frames-in-flight rule lives in the allocators and nowhere else.
3. **Transient bytes are disjoint per frame-in-flight.** A slot's bytes are never reused before that frame's fence has signalled. This is the exact hazard that caused the CSM shadow wiggle (`vk_renderer_globals.cpp:50-52`).
4. **Transient descriptors are written per frame at bind time, never baked once.** This is what allows the ring to be host-visible where the globals buffer could not be — the globals buffer's device address is baked into heap slots at init and must never move.
5. **Every conversion deletes state from `Renderer3DData`.** If a migration doesn't shrink `renderer_3d_internal.h`, stop and re-evaluate the abstraction.
6. **Public API is API-agnostic** (`renderer/` interface + `RendererAPI` dispatch, mirroring `StorageBuffer::create`); the Vulkan implementation lives in `platform/vulkan/`.

## Findings that shape the design

- **The lifetime model already exists, for descriptors.** `VulkanDescriptorHeap` has `allocate_transient_bytes(size, align)`, per-frame-slot bump cursors, `begin_frame(frame_in_flight)`, a fixed per-frame stride budget, and throttled high-water logging (`vk_descriptor_heap.h:19-64`). The buffer layer needs the same thing; mirror its structure deliberately.
- **Four hand-rolled per-frame patterns exist today**: the descriptor heap transient region; `VulkanRendererGlobals` (mirror → per-frame staging → device-local copy); `VulkanIconGlobals` (device-local suballocator with bump cursors, a freelist keyed by `(band_count<<32|curve_count)`, and a mutex); and module-local `[frame_slot]` arrays.
- **No suballocator, no VMA.** Every `StorageBuffer::create` is a `vkCreateBuffer` + its own `vkAllocateMemory` (`vk_buffer.cpp:361-420`). `maxMemoryAllocationCount` is commonly 4096, and `renderer_3d_vector_icon.cpp:105` allocates device memory *inside pass execution* whenever icon count grows.
- **Every dynamic upload maps and unmaps** (`vk_buffer.cpp:494-500`). Host-coherent memory should be mapped once at creation.
- **Correction from the discussion:** meshlet's `indirect_buffers`/`count_buffers` are **CPU-written**, not GPU-written — `StorageBufferUsage::Dynamic | Indirect` with `set_data` at `renderer_3d_meshlet.cpp:210-211`. They are class 1 (transient), not class 3.
- **Transient allocations are not always pass-private.** `flush_meshlet_draws` fills the indirect buffer during the GBuffer pass; `shadow.draw` and `shadow.draw_directional` read it afterwards (`renderer_3d_internal.h:116`, pass order in `deferred.hnfg:86,93,100`). The allocator therefore needs **frame-scoped named allocations** retrievable from a later executor, not just per-pass slices.
- **Indirect draws need a `VkBuffer` + offset**, not just a device address (`s_fn_draw_mesh_tasks_indirect(cmd, indirect_vk, ...)` at `renderer_3d_shadow.cpp:277,616`). The returned slice must carry both.
- **Migration surface is small**: 18 `StorageBuffer::create` call sites across 6 files.

## The four allocation classes

| Class | Written by | Lifetime | Backing | Examples |
|---|---|---|---|---|
| **Transient** | host, per frame | until this frame slot recycles | ring, bump, never individually freed | icon instances, meshlet indirect+count |
| **Persistent** | host, once/rarely | until released | suballocated device-local + staging | mesh vertex/index, icon curves & bands, materials |
| **Graph** | GPU pass | pass→pass within a frame | frame graph (barriers, aliasing) | cloth ping-pong, SSAO targets |
| **Readback** | GPU | until fence signals | pooled, fence-gated | *(deferred — zero users today)* |

`StorageBufferUsage` currently conflates lifetime (`Default`/`Dynamic`/`Immutable`/`Readback`) with binding capability (`VertexBuffer`/`Indirect`/`RTGeometry`). Capability stays as flags; lifetime becomes the class.

## Library decision

**Hand-roll the transient ring. Adopt VMA for the persistent class, in a later phase.**

The ring needs three large buffers allocated once at init and persistently mapped — VMA contributes nothing there, and this is the piece specific to the engine. General device-memory allocation is the opposite: a solved problem with subtle failure modes, currently costing an allocation per buffer, and it's what VMA exists for. Vendoring is cheap — it's a single header, added as `Honey/vendor/vma` following the existing `add_subdirectory` convention (`Honey/CMakeLists.txt:67-72`) with one `.cpp` defining `VMA_IMPLEMENTATION`.

Keeping these in separate phases matters: adopting VMA touches every buffer and image allocation in the engine, and entangling that blast radius with a new API would make both hard to debug.

---

## Phase 1 — Transient allocator

**New files**
- `Honey/engine/src/Honey/renderer/transient_buffer.h` — API-agnostic interface + slice type
- `Honey/engine/src/platform/vulkan/vk_transient_buffer.{h,cpp}` — Vulkan implementation
- OpenGL: `create_transient_allocator` returns a stub that asserts on `allocate`. None of the frame-graph/heap paths run on OpenGL; a real implementation can follow if that backend revives.

**Slice type** — mirrors the existing `get_native_buffer()` → `void*` convention:

```cpp
struct TransientSlice {
    void*    mapped         = nullptr;  // write-only host pointer
    void*    native_buffer  = nullptr;  // VkBuffer, for indirect draws
    uint64_t device_address = 0;        // BDA, for descriptor writes
    uint32_t offset         = 0;        // byte offset within native_buffer
    uint32_t size           = 0;
    explicit operator bool() const { return mapped != nullptr; }
};
```

**Interface**

```cpp
class TransientBufferAllocator {
public:
    virtual void           begin_frame(uint32_t frame_in_flight) = 0;
    virtual TransientSlice allocate(uint32_t size, uint32_t align) = 0;
    virtual void           publish(std::string_view name, const TransientSlice&) = 0;
    virtual TransientSlice lookup(std::string_view name) const = 0;   // frame-scoped
    static Scope<TransientBufferAllocator> create();                  // RendererAPI dispatch
};
```

**Implementation notes**
- One ring buffer per frame in flight, created with all usage bits at once (`STORAGE | UNIFORM | INDIRECT | VERTEX | INDEX | SHADER_DEVICE_ADDRESS`) — `VulkanStorageBuffer::allocate` already sets storage+uniform+BDA unconditionally, so this follows existing practice. No usage parameter needed on `allocate`; alignment is the only per-call variable.
- **Memory type:** probe for `HOST_VISIBLE | HOST_COHERENT | DEVICE_LOCAL` (the BAR/ReBAR window — CPU writes stream across PCIe once, GPU reads at full VRAM bandwidth) and fall back to `HOST_VISIBLE | HOST_COHERENT`. Note `VulkanUtils::find_memory_type` (`vk_utils.h:8`) **asserts** on failure and cannot be used for probing; a non-asserting variant returning `UINT32_MAX` is needed. Log which type was selected.
- Persistently mapped at creation. Never unmapped.
- `begin_frame` resets that slot's bump cursor and clears the slot's name registry. Called from `VulkanContext::begin_frame_recording`, immediately beside the existing `get_descriptor_heap()->begin_frame(m_current_frame)` (`vk_context.cpp:611`). **Verify that call site sits after the frame's fence wait** — invariant 3 rests on it.
- **Mapped memory is write-only.** Host-coherent memory is typically write-combined; reads are catastrophically slow. Document on the API and don't `memset` or accumulate into it.
- Align each suballocation to `minStorageBufferOffsetAlignment` / `minUniformBufferOffsetAlignment`, reusing `VulkanUtils::align_up` (`vk_utils.h:104`). This is the direct analogue of `descriptor_alignment(type)` in the heap.
- **Threading:** decide whether `allocate` needs a mutex before writing it. The descriptor heap holds `m_alloc_mutex`; the bindless freelist slot-collision bug was a thread-unsafe allocator. An uncontended mutex over a handful of allocations per frame costs nothing measurable.
- **Overflow policy (self-healing):** record the requested high-water, return a null slice with a warning (callers early-out, dropping that frame's draw), and grow the ring at the next `begin_frame` for that slot — when its fence is known signalled. One frame of visual glitch, no crash, no manual tuning. Note that growth affects one slot at a time while others are still in flight; either grow each slot lazily as it comes around or keep old buffers alive until every slot has cycled, and record the reasoning in a comment. Reuse `Honey/core/timer.h` for throttled high-water logging exactly as the heap does.
- Initial budget: **4 MiB per frame slot**. `GPUIconInstance` is 96 bytes and meshlet indirect is 12 bytes per draw, so this is generous; the high-water log will show the real number.
- `create_buffer` (`vk_buffer.cpp:15`) is file-static and cannot be reused as-is. Six sites already open-code the same create+allocate+bind sequence; promoting it into `VulkanUtils` is worth doing here.

**What to mirror from `VulkanDescriptorHeap`, and what not to.** Copy the per-frame-slot structure, the `begin_frame(frame_in_flight)` shape, the `Timer`-throttled high-water logging, and the convention of a named budget constant carrying a comment on how it was measured. Do **not** copy the shadow table, bindless indices, sampler heap, persistent region, or `debug_verify` — the heap is complicated because descriptors lack driver validation under `VK_EXT_descriptor_heap`. A byte ring needs none of it.

**Frame-graph integration** — `FrameGraphPassContext` (`frame_graph.h`) gains:

```cpp
template<typename T> std::span<T> reserve(std::string_view shader_name, uint32_t count);
template<typename T> void         upload(std::string_view shader_name, std::span<const T>);
TransientSlice                    transient(std::string_view name) const;  // cross-pass lookup
```

`reserve`/`upload` allocate from the ring, record `{shader_name → slice}` in a per-pass pending-uploads map, publish to the frame registry, and return the span. `bind_heap_pipeline` (`frame_graph.cpp:960-1020`) gains a step after its graph-resource loop: walk pending uploads, match each by shader name against the plan's entries (the plan already carries binding and `block_offset`), and write the descriptor at the reflected offset. Entries still unwritten after both loops keep the existing warning — which now means something real.

**Rule:** uploads must precede `bind_heap_pipeline` within a pass. This is naturally satisfied (reserve outside the command lambda, bind inside); assert if violated.

For non-executor code, add a renderer-level accessor — `flush_meshlet_draws` is called from `renderer_3d.cpp:148,151`, not from a frame-graph executor, so it needs the allocator without a `FrameGraphPassContext`.

## Phase 2 — Migrate the three modules

**2a. Vector icons** — `renderer_3d_vector_icon.cpp`
- Replace lines 99-133 with a single `reserve<GPUIconInstance>("IconInstances", total_draws)`, filling the returned span directly and dropping the intermediate `std::vector` + memcpy at lines 78-97.
- Delete the manual heap write (124-133) and its explanatory comment.
- Delete `icon_instance_buffers` from `renderer_3d_internal.h:77`.
- The `.hnfg` is untouched — `IconInstances` never appears there.

**2b. Meshlet** — `renderer_3d_meshlet.cpp:123-133, 207-211`
- One slice each for the frame's indirect and count data; the per-mesh loop writes at offsets into `slice.mapped` instead of calling `set_data`.
- `publish("meshletIndirect", ...)` / `publish("meshletCount", ...)` so the shadow passes can find them.
- Delete `indirect_buffers` and `count_buffers` from `renderer_3d_internal.h:73-74`.

**2c. Shadow** — `renderer_3d_shadow.cpp`
- Replace `data->indirect_buffers[frame]` null-checks (~202, ~543) with `ctx.transient("meshletIndirect")`; a falsy slice preserves the existing early-out semantics ("meshlet pass produced nothing").
- Replace the two `get_native_buffer()` casts (~248, ~588) with `slice.native_buffer`.
- Delete the two `const uint32_t frame = ... % k_max_frames_in_flight` lines (137, 455).

> **Highest-risk item in the whole migration.** `indirect_byte_off` is computed in meshlet (`renderer_3d_meshlet.cpp:207`), stored into `ShadowDrawEntry` (line 229), and consumed as the indirect offset by the shadow passes (`renderer_3d_shadow.cpp:277,616`). With a ring these offsets become slice-relative and must gain `slice.offset`. **Recommendation:** bake `slice.offset` in at the meshlet end so the stored field consistently means "absolute offset in `native_buffer`" and the shadow diff stays limited to the buffer-handle lookup. Get this wrong and the symptom is corrupted shadows only — geometry renders fine, because meshlet's own draws use the same (wrong-by-the-same-amount) base.

## Phase 3 — Persistent class + VMA

- Vendor VMA at `Honey/vendor/vma` (single header + one `VMA_IMPLEMENTATION` TU), wired via `add_subdirectory` alongside the existing vendored deps.
- Introduce `GpuArray<T>` — typed, device-local, suballocated, with `allocate(count) → GpuRange{offset, count}`, `write(range, data, count)` via staging, and `free(range)`.
- **Absorb `VulkanIconGlobals`** (`vk_icon_globals.h:18-58`): its bump cursors, size-keyed freelist, mutex, and region math are all generic. `IconBandTable` and `IconCurves` then leave `global_bindings.h` and become module-private, which is where they belong — they're in set 0 today only because there was no per-module persistent path.
- Route `gltf_loader.cpp` mesh buffers and the remaining `Immutable` `StorageBuffer::create` sites through VMA.
- `VulkanRendererGlobals` **stays as-is**. Its mirror → staging → device-local design is deliberate and documented (`vk_renderer_globals.cpp:50-52`); its device address is baked into heap slots at init and must never move.

## Explicitly deferred

- **Path tracer** (`renderer_3d_pathtracer.cpp`) — its `PerFrameSlot frame_slots[k_pt_frames]` carry cross-frame accumulation state, a genuinely different lifetime category. Revisit once the four classes are proven; it may justify a fifth.
- **Readback class** — zero users today. `StorageBufferUsage::Readback` is parsed by `frame_graph_loader.cpp:343` and never constructed. When a consumer appears, the design rule is: never map-and-wait; submit a request, poll N frames later.
- **Frame-graph `Buffer` resource per-frame pooling** — class 3 still allocates one buffer per resource at compile time (`frame_graph.cpp:1889`). Fine while its only users are GPU-written (cloth) or write-once (`ssaoKernel`), but it should get frame-slot pooling before any per-frame host-written graph buffer is added.

## Verification

Build via CLion/CMake. **Run with cwd = `cmake-build-debug`** (`./Honey_Editor` from inside that directory) — the content browser hardcodes `../assets` and the editor crashes otherwise.

Per phase:

1. **Phase 1 standalone checkpoint** — wire the ring in with *zero callers*. Expect one allocation log line at startup naming the selected memory type, no per-frame allocations, no validation errors, and a high-water line reporting 0 bytes used.
2. **Validation layers clean** across a full editor session — no new sync or descriptor errors. The descriptor-heap path has no driver-side descriptor validation, so also confirm the existing `debug_verify` safety net (`vk_descriptor_heap.h:57`) still passes in an assert-enabled build.
3. **Vector icons** — load a scene with lights/cameras and confirm icons render identically to before, including multi-shape SVGs and correct occlusion behind geometry. Confirm the `bind_heap_pipeline` warning for `IconInstances` is gone.
4. **Meshlet + shadows together** — this is where the offset bug would surface. Load a scene with several distinct meshes plus a directional light and at least one shadow-casting point light (`assets/scenes/`; Bistro or FlightHelmet exercise real draw counts). Verify shadows match a pre-change build; corrupted-but-plausible shadows with correct geometry is the specific failure signature.
5. **Camera motion under CSM** — the previous host-visible hazard manifested as shadow wiggle under camera movement, not as a static artifact. Pan and orbit continuously for several seconds and watch cascade boundaries.
6. **Frame-slot cycling** — run for more than `k_max_frames_in_flight` frames with varying icon and draw counts (add/remove entities live) so the ring wraps and grows.
7. **Graph rebuild** — resize the viewport repeatedly and toggle `RendererType`/`GeometryPath` in `config/settings.yaml`; confirm the ring survives frame-graph recompilation (it is device-lifetime, not graph-lifetime).
8. **Budget sanity** — read the throttled high-water log and right-size the 4 MiB initial budget, mirroring how the descriptor heap's constant was tuned from a Bistro reading (`vk_descriptor_heap.h:194-198`).
9. **Invariant 5 check** — confirm `renderer_3d_internal.h` lost three `std::array<Ref<StorageBuffer>, k_max_frames_in_flight>` members (lines 73, 74, 77) and that no `frame_slot` remains in `renderer_3d_vector_icon.cpp`, `renderer_3d_meshlet.cpp`, or `renderer_3d_shadow.cpp`.
