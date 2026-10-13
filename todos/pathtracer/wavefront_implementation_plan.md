# Wavefront Path Tracer Implementation Plan

## Why Bother

Your NSight trace tells the story directly:

| Metric | Value | What It Means |
|--------|-------|---------------|
| SM Throughput | **26.9%** | SMs are mostly idle |
| Active Threads / Warp | **11.0 / 32** | Only 34% of lanes doing work |
| CS SM Warp Occupancy | **34.7%** | Low resident warp count |

Those 11 active threads per warp are the symptom the Laine et al. paper was written about. In your megakernel `PathTrace_ClosestHit.rchit`, every warp processes 32 paths simultaneously. Each path may be at a different bounce, hit a different material, or already be terminated — so the warp is constantly masking lanes. Moving to a wavefront formulation groups paths by *what they need to do next*, so every lane in a warp is doing the same work.

The paper reports: **36% speedup** on a simple single-material scene, **79%** on moderate complexity, and **221%** on a scene with multiple expensive materials. Your bistro scene has many materials so you should expect to land in the upper half of that range.

---

## Recommended Reading (Before You Start)

Read these in order, each builds on the last:

1. **Laine, Karras, Aila (2012) — "Megakernels Considered Harmful"** ← you already have this  
   The architecture you are implementing. Re-read sections 4.1, 4.2, and 4.3 carefully. Section 4.3 (Memory Layout) is where most of the actual performance comes from.

2. **Aila & Laine (2009) — "Understanding the Efficiency of Ray Traversal on GPUs"**  
   https://research.nvidia.com/publication/2009-08_understanding-efficiency-ray-traversal-gpus  
   Explains why ray cast throughput is bounded by memory latency, not compute. Essential context for sizing your path pool.

3. **Sebastien Hillaire (2020) — "A Primer on Efficient Rendering Algorithms & Pathtracing"**  
   https://www.elopezr.com/a-primer-on-efficient-rendering-algorithms-and-pathtracing/  
   Useful overview of the wavefront concept with diagrams more modern than the 2012 paper.

4. **Vulkan Ray Tracing Best Practices (NVIDIA)**  
   https://developer.nvidia.com/blog/best-practices-using-nvidia-rtx-ray-tracing/  
   Particularly the section on avoiding recursive ray tracing and keeping shaders lean.

5. **GLSL `GL_KHR_shader_subgroup` spec** — you will need subgroup ballot ops for warp-coalesced queue writes (Section 8 of the plan).

---

## Architecture Overview

### Current (Megakernel)

```
[RayGen.rgen] ─ bounce loop ─► [ClosestHit.rchit]
                                  ├─ vertex decode
                                  ├─ material eval
                                  ├─ shadow traceRayEXT × N lights
                                  └─ bounce direction
```

One RT pipeline. All work per-path per-bounce happens in a single kernel launch. Control flow diverges inside every warp.

### Target (Wavefront)

```
Frame N iteration:
  ① [Logic.comp]       ─ per path: accumulate radiance, apply RR, check termination
  ② [NewPath.comp]     ─ per terminated path: initialize new camera ray, push to extend queue
  ③ [Material.comp]    ─ per hit path: eval BRDF, push extend ray + shadow ray to queues
  ④ [Extend.rgen]      ─ trace all extension rays from queue, write HitRecord buffer
  ⑤ [Shadow.rgen]      ─ trace all shadow rays from queue, write occlusion bit buffer
  (loop back to ①)
```

Five kernels. Each kernel operates on a uniform set of paths doing identical work. Warp coherence approaches 100% within each kernel.

The key insight from the paper: **path state lives in GPU SSBO memory, not in registers or ray payload**. This makes every kernel stateless — it reads state, does one unit of work, writes state back.

---

## Data Structures to Design First

Design these on paper before writing any shader code. Everything else follows from them.

### Path State Buffer (SOA layout — critical)

The paper shows an **80% speedup** just from switching AOS → SOA. The reason: in the logic kernel, all threads access the same field (e.g., `throughput`) across consecutive paths. With SOA, this is a coalesced 128-byte read across a warp. With AOS it is strided.

```glsl
// SOA path state — one flat buffer per field, N paths total
// N = viewport_width * viewport_height (one path per pixel, regenerated on termination)

layout(binding = X, std430) buffer PathRayOrigin    { vec3  ray_origin[N]; };
layout(binding = X, std430) buffer PathRayDir       { vec3  ray_dir[N]; };
layout(binding = X, std430) buffer PathThroughput   { vec3  throughput[N]; };
layout(binding = X, std430) buffer PathRadiance     { vec3  radiance[N]; };      // accumulated
layout(binding = X, std430) buffer PathSeed         { uint  seed[N]; };
layout(binding = X, std430) buffer PathPixel        { uint  pixel_index[N]; };   // which pixel this path writes to
layout(binding = X, std430) buffer PathBounce       { uint  bounce[N]; };
layout(binding = X, std430) buffer PathFlags        { uint  flags[N]; };         // TERMINATED bit, etc.
```

Note: `vec3` in std430 is padded to 16 bytes. Either use `vec4` and waste a component, or pack as `float` arrays (`float ray_origin_x[N]; float ray_origin_y[N]; float ray_origin_z[N];`). The pure float-array form has better coalescing. Benchmark both — the vec4 form is easier to read.

### Hit Record Buffer

Written by `Extend.rgen` / `ClosestHit.rchit`, read by `Material.comp`. One record per path in the extension queue.

```glsl
struct HitRecord {
    vec3  hit_pos;           // world-space hit point
    float hit_t;             // ray parameter at hit
    vec3  geo_normal;        // geometry normal (Ng)
    uint  instance_id;       // gl_InstanceCustomIndexEXT → geometry + material lookup
    vec2  barycentrics;
    uint  primitive_id;
    uint  hit_kind;          // 0 = miss (sky), 1 = hit
};
layout(binding = X, std430) buffer HitRecords { HitRecord records[]; };
```

### Queue Buffers

Each queue is a flat buffer of `uint` path indices plus an atomic counter.

```glsl
// Extension ray queue: paths whose next bounce ray needs to be traced
layout(binding = X, std430) buffer ExtendQueue       { uint  extend_paths[N]; };
layout(binding = X, std430) buffer ExtendQueueCount  { uint  extend_count;     };

// Shadow ray queue: one entry per shadow ray needed
struct ShadowRayRequest {
    uint  path_index;
    vec3  origin;
    vec3  direction;
    float t_max;
    uint  light_index;    // which light this tests
};
layout(binding = X, std430) buffer ShadowQueue      { ShadowRayRequest shadow_rays[N * MAX_SHADOW_RAYS_PER_PATH]; };
layout(binding = X, std430) buffer ShadowQueueCount { uint shadow_count; };

// Occlusion results: written by shadow RT kernel, indexed by ShadowRayRequest
layout(binding = X, std430) buffer OcclusionBuffer  { uint occluded[N * MAX_SHADOW_RAYS_PER_PATH]; };

// Accumulation write queue: paths that are done and want to write to the pixel buffer
// (can fold this into PathFlags — just check TERMINATED flag in a post-pass)
```

---

## Phase 1 — Thin RT Shaders

**Goal:** Strip all material evaluation out of the RT shaders. The RT pipeline becomes purely a ray-intersection query. The closest hit shader writes a `HitRecord` and exits. Material evaluation moves to compute.

### 1a. New Extension RayGen (`PathTrace_Extend.rgen`)

Replace the current bounce-loop raygen. This shader:
- Reads one entry from `ExtendQueue`
- Reads that path's `ray_origin` and `ray_dir` from the path state SOA
- Calls `traceRayEXT` once
- The hit record is written by the new thin closest-hit shader

```glsl
#version 460
#extension GL_EXT_ray_tracing : require

// HitRecord written by ClosestHit, read back by Material.comp
struct HitRecord { /* as above */ };
layout(location = 0) rayPayloadEXT HitRecord hit_record;

layout(binding = X, std430) readonly  buffer ExtendQueue      { uint extend_paths[]; };
layout(binding = X, std430) readonly  buffer ExtendQueueCount { uint extend_count;   };
layout(binding = X, std430) readonly  buffer PathRayOriginBuf { vec3 ray_origin[];   };
layout(binding = X, std430) readonly  buffer PathRayDirBuf    { vec3 ray_dir[];      };
layout(binding = X, std430) writeonly buffer HitRecordBuf     { HitRecord records[]; };

void main() {
    uint queue_slot = gl_LaunchIDEXT.x;
    if (queue_slot >= extend_count) return;

    uint path_idx = extend_paths[queue_slot];

    hit_record.hit_kind = 0; // miss by default — miss shader leaves this alone or writes 0
    traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF, 0, 0, 0,
                ray_origin[path_idx], 0.001, ray_dir[path_idx], 10000.0, 0);

    records[queue_slot] = hit_record;
}
```

Note: `gl_LaunchSizeEXT` here is `(extend_count, 1, 1)` — you dispatch exactly as many threads as there are rays in the queue.

### 1b. Thin ClosestHit (`PathTrace_ClosestHit.rchit`)

Remove everything except the geometry query. Write one `HitRecord` to the payload. No material eval, no shadow rays, no BRDF.

```glsl
void main() {
    hit_record.hit_t       = gl_HitTEXT;
    hit_record.barycentrics = barycentrics;
    hit_record.instance_id  = gl_InstanceCustomIndexEXT;
    hit_record.primitive_id = gl_PrimitiveID;
    hit_record.hit_kind     = 1; // hit

    // Geometry normal only — enough for material compute to reconstruct full shading normal
    GeometryInfo geo = u_geometry.entries[gl_InstanceCustomIndexEXT];
    // ... (same vertex decode as current, just normal, no TBN, no UV yet)
    // Actually, defer ALL of this to Material.comp — just pass barycentrics + IDs
    // Material.comp has access to the same geometry SSBOs
    hit_record.geo_normal = compute_geo_normal(); // or skip — material compute can do it
}
```

A simpler option for the initial port: keep the current HitRecord as just `{barycentrics, instance_id, primitive_id, hit_t, hit_kind}` and do all vertex decode in Material.comp. This keeps the hot RT shader register count as low as possible.

### 1c. Miss Shader (`PathTrace_Miss.rmiss`)

Mark the hit record as a miss. Sky radiance evaluation moves to Material.comp (or Logic.comp when handling terminated paths).

```glsl
void main() {
    hit_record.hit_kind = 0; // miss — material kernel will evaluate sky
}
```

### 1d. Shadow RayGen (`PathTrace_Shadow.rgen`)

Consumes `ShadowQueue`, writes one bit per entry to `OcclusionBuffer`.

```glsl
void main() {
    uint slot = gl_LaunchIDEXT.x;
    if (slot >= shadow_count) return;

    ShadowRayRequest req = shadow_rays[slot];
    shadow_hit = false; // payload bool
    traceRayEXT(tlas,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
        0xFF, 0, 0, 1, // shadow miss shader index = 1
        req.origin, 0.001, req.direction, req.t_max, 0);
    occluded[slot] = shadow_hit ? 1u : 0u;
}
```

---

## Phase 2 — Material Compute Kernel (`PathTrace_Material.comp`)

This is the ported body of your current `PathTrace_ClosestHit.rchit`, running as a compute shader.

Input: `HitRecords[]`, `ExtendQueue[]` (path indices), full path state SOA.  
Output: Updated `throughput[]`, updated `radiance[]` (via direct light contribution), new `ray_dir[]`, entries written to `ShadowQueue`.

```glsl
#version 460
#extension GL_KHR_shader_subgroup_arithmetic : require // for coalesced queue writes (Phase 8)

layout(local_size_x = 64) in;

void main() {
    uint slot = gl_GlobalInvocationID.x;
    if (slot >= extend_count) return;

    uint path_idx = extend_paths[slot];
    HitRecord rec = hit_records[slot];

    if (rec.hit_kind == 0) {
        // Miss — path escaped. Accumulate sky and terminate.
        radiance[path_idx] += throughput[path_idx] * evaluate_sky(ray_dir[path_idx]);
        flags[path_idx]    |= FLAG_TERMINATED;
        return;
    }

    // Vertex decode (same code as current rchit)
    // ...

    // Material eval (same BRDF code as current rchit)
    // ...

    // Direct light contribution:
    //   Instead of traceRayEXT here, push shadow ray requests to ShadowQueue.
    //   We'll consume OcclusionBuffer in the Logic kernel next iteration.
    uint shadow_slot = atomicAdd(shadow_count, num_shadow_rays);
    shadow_rays[shadow_slot] = ShadowRayRequest(path_idx, hit_pos, L, dist, light_id);

    // Store pending direct light (we don't know if it's occluded yet).
    // Simplest: store the full unoccluded Lo in a PendingRadiance buffer,
    // Logic kernel adds it to radiance[path_idx] only if occluded[shadow_slot] == 0.

    // Bounce direction
    ray_dir[path_idx]    = bounce_dir;
    ray_origin[path_idx] = hit_pos + N * 0.001;
    throughput[path_idx] *= bounce_throughput;
    bounce[path_idx]++;
}
```

**Important implementation note on shadow rays:** The deferred shadow ray + pending radiance pattern adds one frame of latency to direct lighting. For the initial implementation, simplify by computing direct lighting in the *next* Logic pass after shadows are resolved. Alternatively (and more correct), resolve shadows immediately within the same frame by ordering the dispatch: Material → Shadow RT → Logic reads OcclusionBuffer. This is the correct ordering and avoids the latency issue.

---

## Phase 3 — Logic Compute Kernel (`PathTrace_Logic.comp`)

Runs *before* Material each iteration. Responsibilities:
- Consume `OcclusionBuffer` from last frame's shadow pass, add unoccluded direct light to `radiance[]`
- Apply Russian roulette
- Push non-terminated paths to `ExtendQueue`
- Push terminated paths to a pixel write-out list (or directly write to accumulation image)

```glsl
layout(local_size_x = 64) in;

void main() {
    uint path_idx = gl_GlobalInvocationID.x;
    if (path_idx >= total_path_count) return;

    // Resolve pending shadow contributions
    // (iterate over this path's shadow slots, check occluded[], add to radiance[])
    for (uint i = path_shadow_start[path_idx]; i < path_shadow_end[path_idx]; i++) {
        if (occluded[i] == 0)
            radiance[path_idx] += throughput[path_idx] * pending_direct[i];
    }

    if (flags[path_idx] & FLAG_TERMINATED) {
        // Write final radiance to accumulation buffer
        write_to_accum(pixel_index[path_idx], radiance[path_idx]);
        // Mark for regeneration — NewPath kernel will handle this
        flags[path_idx] |= FLAG_NEEDS_REGEN;
        return;
    }

    // Russian roulette
    if (bounce[path_idx] >= 2) {
        float rr_prob = max3(throughput[path_idx]);
        rr_prob = clamp(rr_prob, 0.05, 1.0);
        if (rand_f(seed[path_idx]) > rr_prob) {
            flags[path_idx] |= FLAG_TERMINATED;
            return;
        }
        throughput[path_idx] /= rr_prob;
    }

    // Push to extend queue
    uint slot = atomicAdd(extend_count, 1);
    extend_paths[slot] = path_idx;
}
```

---

## Phase 4 — New Path Compute Kernel (`PathTrace_NewPath.comp`)

Initializes terminated paths with a new camera ray. This is also responsible for writing the pixel-space seed and assigning the pixel index.

```glsl
layout(local_size_x = 64) in;

// Push constant: inv_view, inv_proj, frame_count, viewport_width/height
layout(push_constant) uniform PC { mat4 inv_view; mat4 inv_proj; uint frame_count; uint width; uint height; };

void main() {
    uint path_idx = gl_GlobalInvocationID.x;
    if (path_idx >= total_path_count) return;
    if (!(flags[path_idx] & FLAG_NEEDS_REGEN)) return;

    // Same pixel mapping as current raygen
    uint px = path_idx % width;
    uint py = path_idx / width;
    pixel_index[path_idx] = path_idx;

    // Reinitialize seed
    seed[path_idx] = wang_hash(px ^ wang_hash(py ^ wang_hash(frame_count + 1u)));

    // Generate camera ray
    vec2 uv = (vec2(px, py) + 0.5) / vec2(width, height) * 2.0 - 1.0;
    uv.y = -uv.y;
    vec4 origin    = inv_view * vec4(0,0,0,1);
    vec4 target    = inv_proj * vec4(uv, 1, 1);
    vec4 direction = inv_view * vec4(normalize(target.xyz), 0);

    ray_origin[path_idx]  = origin.xyz;
    ray_dir[path_idx]     = normalize(direction.xyz);
    throughput[path_idx]  = vec3(1.0);
    radiance[path_idx]    = vec3(0.0);
    bounce[path_idx]      = 0;
    flags[path_idx]       = 0;

    // Push to extend queue so this path gets traced this frame
    uint slot = atomicAdd(extend_count, 1);
    extend_paths[slot] = path_idx;
}
```

---

## Phase 5 — C++ Pipeline Wiring

This is the most involved implementation work. The current `renderer_3d_pathtracer.cpp` drives one RT pipeline per frame. You need to replace it with a per-frame dispatch sequence.

### New Dispatch Order (one "iteration" per frame)

```cpp
void record_wavefront_frame(VkCommandBuffer cmd) {

    // 0. Clear queue counters (write 0 to extend_count, shadow_count)
    vkCmdFillBuffer(cmd, queue_count_buffer, 0, sizeof(uint32_t) * 2, 0);
    // Pipeline barrier: transfer write → shader read/write

    // 1. Logic kernel — resolve shadows, apply RR, push to extend queue
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, logic_pipeline);
    vkCmdDispatch(cmd, ceil(total_paths / 64.0f), 1, 1);
    // Barrier: compute write → compute read + transfer read

    // 2. New Path kernel — regenerate terminated paths, push to extend queue
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, new_path_pipeline);
    vkCmdDispatch(cmd, ceil(total_paths / 64.0f), 1, 1);
    // Barrier: compute write → indirect read + rt read

    // 3. Extend RT — trace all rays in extend queue
    //    Launch size = extend_count (indirect dispatch from buffer)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, extend_rt_pipeline);
    // vkCmdTraceRaysIndirectKHR if you want exact count from GPU buffer
    // or: read extend_count back to CPU and use vkCmdTraceRaysKHR (adds GPU→CPU sync, avoid)
    vkCmdTraceRaysIndirectKHR(cmd, &sbt_raygen, &sbt_miss, &sbt_hit, &sbt_callable,
                              indirect_dispatch_buffer_addr); // contains {extend_count, 1, 1}
    // Barrier: rt write → compute read

    // 4. Material kernel — BRDF eval, push shadow rays
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, material_pipeline);
    vkCmdDispatch(cmd, ceil(total_paths / 64.0f), 1, 1);
    // Barrier: compute write → rt read

    // 5. Shadow RT — trace all shadow rays
    vkCmdTraceRaysIndirectKHR(cmd, &sbt_shadow_raygen, ...);
    // Barrier: rt write → compute read (next frame's Logic kernel reads OcclusionBuffer)
}
```

**Critical:** `vkCmdTraceRaysIndirectKHR` requires `VK_KHR_ray_tracing_maintenance1` and a buffer containing `VkTraceRaysIndirectCommandKHR { width, height, depth }`. Check your `vk_context.cpp` feature negotiation and add this extension. You already have `VK_KHR_ray_tracing_pipeline` — maintenance1 should be available alongside it on any recent driver.

If `vkCmdTraceRaysIndirectKHR` is unavailable, the fallback is `vkCmdTraceRaysKHR` with a fixed launch size equal to `total_paths`. You'll over-launch (threads guard `if (slot >= extend_count) return`), which wastes some SM time but avoids the CPU readback entirely. This is acceptable for initial implementation.

### New Buffers to Allocate in `PathTracerResources`

```cpp
// Path state (SOA — one buffer per field)
VkBuffer path_ray_origin_buf;  // vec4[N] or float[N*3]
VkBuffer path_ray_dir_buf;
VkBuffer path_throughput_buf;
VkBuffer path_radiance_buf;    // accumulated
VkBuffer path_seed_buf;        // uint[N]
VkBuffer path_pixel_buf;       // uint[N]
VkBuffer path_bounce_buf;      // uint[N]
VkBuffer path_flags_buf;       // uint[N]

// Hit records
VkBuffer hit_record_buf;       // HitRecord[N]

// Queues
VkBuffer extend_queue_buf;     // uint[N]
VkBuffer shadow_queue_buf;     // ShadowRayRequest[N * MAX_SHADOW_PER_PATH]
VkBuffer occlusion_buf;        // uint[N * MAX_SHADOW_PER_PATH]
VkBuffer pending_direct_buf;   // vec4[N * MAX_SHADOW_PER_PATH]
VkBuffer queue_count_buf;      // uint[2]: {extend_count, shadow_count}

// Indirect dispatch
VkBuffer indirect_extend_buf;  // VkTraceRaysIndirectCommandKHR (written by a fill/blit from extend_count)
VkBuffer indirect_shadow_buf;
```

### Descriptor Set Layout Changes

You'll need separate descriptor sets (or a large unified set) for each pipeline stage. The simplest approach:

- **Set 0** (shared): TLAS, textures, geometry SSBO, material SSBO, lights UBO
- **Set 1** (path state): all path SOA buffers
- **Set 2** (queues): extend queue, shadow queue, hit records, occlusion buffer

Bind the appropriate sets to each pipeline. Compute pipelines don't need Set 0's TLAS, but sharing the layout with RT pipelines avoids descriptor set churn.

### New Pipelines to Create

```cpp
VkPipeline logic_pipeline;        // compute
VkPipelineLayout logic_layout;
VkPipeline new_path_pipeline;     // compute
VkPipeline material_pipeline;     // compute
VkPipeline extend_rt_pipeline;    // RT (raygen: Extend.rgen, hit: ThinClosestHit.rchit)
VkPipeline shadow_rt_pipeline;    // RT (raygen: Shadow.rgen, miss: ShadowMiss.rmiss)
```

---

## Phase 6 — Descriptor Set Layout for Indirect Dispatch

To use `vkCmdTraceRaysIndirectKHR`, you need to write `{extend_count, 1, 1}` into a buffer usable as `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`. You can write this with a tiny compute shader that copies `extend_count` into the indirect buffer, dispatched between step 2 and step 3:

```glsl
// PathTrace_WriteIndirect.comp
layout(local_size_x = 1) in;
layout(binding = 0, std430) readonly  buffer QueueCounts { uint extend_count; uint shadow_count; };
layout(binding = 1, std430) writeonly buffer IndirectExtend { uint w; uint h; uint d; } extend_cmd;
layout(binding = 2, std430) writeonly buffer IndirectShadow { uint w; uint h; uint d; } shadow_cmd;
void main() {
    extend_cmd.w = extend_count; extend_cmd.h = 1; extend_cmd.d = 1;
    shadow_cmd.w = shadow_count; shadow_cmd.h = 1; shadow_cmd.d = 1;
}
```

Dispatch this with `vkCmdDispatch(cmd, 1, 1, 1)` between the NewPath and Extend stages. Add a barrier after it: `VK_ACCESS_SHADER_WRITE_BIT → VK_ACCESS_INDIRECT_COMMAND_READ_BIT`.

---

## Phase 7 — Frame Initialization

On the very first frame (or after camera movement resets accumulation), all paths are "terminated and need regeneration". Initialize `flags[i] = FLAG_NEEDS_REGEN | FLAG_TERMINATED` for all paths. The NewPath kernel will fill everything in on iteration 0.

---

## Phase 8 — SOA Memory Layout (Do This, It's Worth 80%)

If you implemented Phase 1–7 using AOS (`struct PathState { vec3 origin; vec3 dir; ... }[N]`), switch now.

The change: instead of one large struct buffer, allocate one `float` array per field:

```cpp
// Instead of:
struct PathState { glm::vec4 origin; glm::vec4 dir; ... };
VkBuffer path_state_buf; // PathState[N]

// Use:
VkBuffer path_origin_x_buf; // float[N]
VkBuffer path_origin_y_buf; // float[N]
VkBuffer path_origin_z_buf; // float[N]
VkBuffer path_dir_x_buf;
// etc.
```

In GLSL, use separate `float[]` bindings and manually swizzle:

```glsl
layout(binding = 0, std430) buffer OriginX { float origin_x[]; };
layout(binding = 1, std430) buffer OriginY { float origin_y[]; };
layout(binding = 2, std430) buffer OriginZ { float origin_z[]; };
// ...
vec3 origin = vec3(origin_x[path_idx], origin_y[path_idx], origin_z[path_idx]);
```

This lets the GPU prefetcher coalesce accesses across a warp: all 32 threads read `origin_x[path_idx + 0..31]`, which is one 128-byte fetch. With AOS they would read every 48 bytes (or whatever your struct stride is), defeating the L2 prefetcher.

---

## Phase 9 — Warp-Coalesced Queue Writes

Uncoalesced `atomicAdd` to `extend_count` from 32 threads serializes writes. The paper calls this out explicitly (Section 4.4) and shows a **40% speedup** from coalescing.

Use `GL_KHR_shader_subgroup_arithmetic`:

```glsl
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_ballot : require

// Instead of:
uint slot = atomicAdd(extend_count, 1);
extend_paths[slot] = path_idx;

// Use:
bool wants_slot = !terminated;
uvec4 ballot    = subgroupBallot(wants_slot);
uint  lane_count = subgroupBallotBitCount(ballot);
uint  lane_rank  = subgroupBallotExclusiveBitCount(ballot);

uint base_slot;
if (subgroupElect()) // only first active lane does the atomic
    base_slot = atomicAdd(extend_count, lane_count);
base_slot = subgroupBroadcastFirst(base_slot);

if (wants_slot)
    extend_paths[base_slot + lane_rank] = path_idx;
```

Apply this pattern everywhere a queue is written: the extend queue, shadow queue, and the regeneration list. Add `VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT` to your subgroup feature requirements in `vk_context.cpp`.

---

## Phase 10 — G-Buffer and SVGF Integration

Your current SVGF denoiser (`PathTrace_SVGF.comp`) reads from `gbuffer_image` (normal + depth) and `accum_image`. In the wavefront version:

- **Depth and normal** are now written by the Logic kernel when a path terminates (it knows the first-hit normal from the HitRecord of bounce 0). Store `primary_normal[path_idx]` and `primary_depth[path_idx]` as path state fields.
- After the iteration, a small compute pass copies these to `gbuffer_image` (unchanged RGBA32F layout).
- `accum_image` is written by the Logic kernel's `write_to_accum()` call (unchanged blending logic).
- SVGF pipeline itself requires no changes.

---

## Suggested Implementation Order

This sequence minimizes time spent with a broken renderer between milestones. Each step compiles and produces correct output before the next step begins.

| Step | What You Do | How You Verify |
|------|-------------|----------------|
| 1 | Allocate path state SOA buffers; initialize them with current raygen values on frame 0 | N/A (no render yet) |
| 2 | Write `PathTrace_NewPath.comp`; replace raygen's camera ray setup with this kernel | Blank screen is OK |
| 3 | Write thin `PathTrace_Extend.rgen` + thin `PathTrace_ClosestHit.rchit`; write HitRecord buffer | Still blank |
| 4 | Write `PathTrace_Material.comp` (no shadow rays yet — mark all paths unoccluded) | First image appears |
| 5 | Write `PathTrace_Logic.comp` (no shadow resolution yet, skip direct light) | Image converges, slightly dark |
| 6 | Add shadow ray queue + `PathTrace_Shadow.rgen`; connect Logic to OcclusionBuffer | Full direct lighting |
| 7 | Switch AOS path state to SOA | Profile; should see L2 improvement |
| 8 | Add subgroup ballot coalescing to all queue writes | Profile; should see SM improvement |
| 9 | Restore SVGF G-buffer write; confirm denoiser still works | Visual regression check |

---

## NSight Metrics to Watch After Each Phase

| Metric | Megakernel Baseline | Target After Wavefront |
|--------|--------------------|-----------------------|
| Active Threads/Warp | 11.0 (34%) | 28–32 (>85%) |
| SM Throughput | 26.9% | 60–80% |
| L2 Throughput | 42.6% | 40–60% (up in material compute) |
| RTCORE Throughput | 66.3% | 70–85% (higher with thin shaders) |
| CS SM Warp Occupancy | 34.7% | 50–65% |

The single biggest jump will come from Step 3→4 (material in compute, no divergence) and Step 7 (SOA). After Step 8 you should be close to the paper's reported numbers.

---

## Known Pitfalls

**Barrier hell.** Each stage transition needs explicit `vkCmdPipelineBarrier2`. The typical mistakes:
- Forgetting the barrier between queue counter clear (TRANSFER_WRITE) and first shader read (SHADER_READ)
- Using `VK_ACCESS_SHADER_READ_BIT` when you need `VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT` for read-modify-write atomics
- Missing the `VK_ACCESS_INDIRECT_COMMAND_READ_BIT` stage for indirect dispatch buffers

**`vec3` padding in std430.** `vec3` aligns to 16 bytes in std430. A `vec3[N]` buffer wastes 4 bytes per element (25% overhead). Use `vec4` with a dummy `.w` or use three `float[N]` buffers (SOA is better anyway).

**Shadow ray count explosion.** With `k_point_lights = 32`, each path can emit up to 33 shadow rays per bounce (32 point + 1 directional + 1 env). A 1920×1080 path pool = ~2M shadow rays per frame at first bounce. Size your shadow queue buffer accordingly: `N_paths * (point_lights + 2)` entries. For initial development, clamp shadow samples to 1 per frame (randomly selected light with MIS weight).

**Indirect dispatch precision.** `vkCmdTraceRaysIndirectKHR` reads `{width, height, depth}` where `width = extend_count`. Make sure `extend_count` is clamped to `N_paths` before writing to the indirect buffer. An unclamped atomic could theoretically exceed bounds if there's a bug elsewhere.

**Accumulation reset.** Camera movement currently calls `invalidate_accumulation()` which resets `frame_count`. In the wavefront version, also clear `radiance[]`, `throughput[]` and set all `flags = FLAG_NEEDS_REGEN` when this fires.