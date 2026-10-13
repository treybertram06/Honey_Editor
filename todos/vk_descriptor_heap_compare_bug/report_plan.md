# Bug Report Plan: comparison samplers ignored by `VK_EXT_descriptor_heap`

**Goal:** Produce a complete, submittable bug report to NVIDIA proving that a sampler with
`compareEnable = VK_TRUE` written into the sampler heap via `vkWriteSamplerDescriptorsEXT`
does **not** perform hardware depth comparison — a `Dref`/shadow fetch returns the raw filtered
depth texel instead of the `0/1` comparison result. The spec requires heap-written samplers to
"function identically" to `VkSampler` objects, so this is a conformance bug, not an API limitation.

This document is everything needed to (A) build a standalone minimal repro, (B) gather all
environment/diagnostic data, and (C) fill in the report template. Work top to bottom.

---

## 0. Summary of the bug (the claim we are proving)

- Create one `VkSamplerCreateInfo` with `compareEnable = VK_TRUE`, `compareOp = VK_COMPARE_OP_LESS_OR_EQUAL`.
- Instantiate it **two ways**: (1) as a real `VkSampler` via `vkCreateSampler`, used through a classic
  descriptor set; (2) baked into the sampler heap via `vkWriteSamplerDescriptorsEXT`, used through
  the `VK_EXT_descriptor_heap` mapping path.
- Sample the **same** depth texture with the **same** reference value through each.
- **Expected:** both return the same comparison result (`ref <= texel ? 1 : 0`, then PCF-filtered).
- **Actual:** the `VkSampler`-object path returns the correct comparison result; the heap-written
  path returns the raw (filtered) depth texel — `compareEnable`/`compareOp` are silently ignored.

Spec basis that this violates (quote in the report):
> "Writing a descriptor via these functions results in a descriptor that functions identically to
> descriptors managed by other descriptor management functions using an object created with the
> create info structure." — `vkWriteSamplerDescriptorsEXT`, Vulkan refpages.

The proposal/spec has **no** carve-out for comparison/`Dref`/shadow samplers (verified — searched
the proposal for `compareEnable`/`Dref`/shadow; nothing). So identical behavior is required.

---

## 1. Environment data (already gathered — paste verbatim into the report)

| Field | Value |
|-------|-------|
| GPU | NVIDIA GeForce RTX 3080 (GA102, PCI `10de:2206`) |
| Driver branch | NVIDIA UNIX **Open** Kernel Module 595.71.05 (Release Build) |
| `driverVersion` (Vulkan) | 595.71.5.0 |
| `driverInfo` | NVIDIA 595.71.05 |
| Device `apiVersion` | 1.4.329 |
| Loader (instance) version | 1.4.341 |
| Vulkan SDK | 1.4.341.1 (`VULKAN_SDK=/home/treybertram/vulkan/1.4.341.1/x86_64`) |
| `VK_EXT_descriptor_heap` | present, **extension revision 1** |
| Dependency | `VK_KHR_maintenance5` (enabled alongside) |
| OS | EndeavourOS (Arch), kernel `7.0.3-arch1-1`, x86_64 |
| Compiler (kernel module build) | GCC 15.2.1 |

> ⚠️ Before submitting, re-run the commands in §3.1 in case the driver/SDK was updated. The driver
> branch (open vs proprietary) matters to NVIDIA — note we are on the **Open** kernel module.

---

## 2. Build the minimal reproduction (standalone, no engine dependency)

Keep it self-contained so NVIDIA can build it without Honey. Target: a single `.cpp` (~300–400 lines)
+ two tiny shaders + a `CMakeLists.txt`. Headless — no swapchain, no window. Output a pass/fail to
stdout and optionally dump the two sampled values.

### 2.1 Files to create (place under `todos/vk_descriptor_heap_compare_bug/repro/`)
- `main.cpp` — the whole repro (see structure below)
- `compare.comp` — compute shader that does both samples and writes results to an SSBO
- `CMakeLists.txt` — links Vulkan, compiles the shader with `glslangValidator`/`glslc`
- `README.md` — one-paragraph build+run, plus expected vs actual output

### 2.2 Repro structure (`main.cpp`)
1. **Instance** — API 1.3+, enable `VK_LAYER_KHRONOS_validation` (so we can also report whether the
   validation layer flags anything — it should NOT, which strengthens "driver bug").
2. **Device** — pick the NVIDIA physical device. Enable:
   - `VK_EXT_descriptor_heap` + feature `VkPhysicalDeviceDescriptorHeapFeaturesEXT::descriptorHeap = VK_TRUE`
   - `VK_KHR_maintenance5` + `maintenance5 = VK_TRUE` (required dep)
   - `VK_KHR_buffer_device_address` / core BDA (`bufferDeviceAddress = VK_TRUE`) — heaps need device addresses
   - Load fn pointers: `vkWriteSamplerDescriptorsEXT`, `vkWriteResourceDescriptorsEXT`,
     `vkCmdBindResourceHeapEXT`, `vkCmdBindSamplerHeapEXT`, `vkCmdPushDataEXT`,
     `vkGetPhysicalDeviceDescriptorSizeEXT`. (Reference for the exact usage: Honey's
     `Honey/engine/src/platform/vulkan/vk_descriptor_heap.cpp`.)
3. **Depth texture** — a small `VK_FORMAT_D32_SFLOAT` 2D image (e.g. 4×4), one mip/layer.
   - Fill it with a **known constant depth**, e.g. `0.5`, via a clear (transition to
     `TRANSFER_DST` → `vkCmdClearDepthStencilImage` with `depth = 0.5` → transition to
     `DEPTH_STENCIL_READ_ONLY_OPTIMAL` / `SHADER_READ_ONLY_OPTIMAL`).
   - Create a depth-aspect `VkImageView`.
4. **One sampler CI** — `compareEnable = VK_TRUE`, `compareOp = VK_COMPARE_OP_LESS_OR_EQUAL`,
   `magFilter = minFilter = VK_FILTER_LINEAR`, `mipmapMode = NEAREST`, address modes
   `CLAMP_TO_EDGE` (border not needed for the repro). This is `make_shadow_cmp_sampler_ci()` minus
   the border color — keep it identical to Honey's if you prefer 1:1 fidelity.
5. **Path A (control): VkSampler object** — `vkCreateSampler` with that CI; bind via a classic
   descriptor set (`sampler2DShadow` = combined image+sampler, or separate `texture2D` + `samplerShadow`
   to mirror Honey exactly — see note in §2.4). Dispatch a compute that samples with `ref = 0.4` and
   `ref = 0.6` and writes both comparison results.
6. **Path B (subject): heap-written sampler** — build a sampler heap + resource heap (mirror
   `VulkanDescriptorHeap`), `vkWriteSamplerDescriptorsEXT` the **same CI** into the sampler heap,
   write the depth image as a sampled-image descriptor into the resource heap, bind both heaps with
   `vkCmdBindSamplerHeapEXT`/`vkCmdBindResourceHeapEXT`, map shader set/binding → heap offsets
   (constant-offset mapping, as in `vk_descriptor_mapping.cpp`), push data, dispatch the **same**
   shader with the **same** `ref` values.
7. **Readback** — copy the SSBO results to host, print:
   ```
   ref=0.4  expected(<=0.5)=1.0   objectPath=__   heapPath=__
   ref=0.6  expected(<=0.5)=0.0   objectPath=__   heapPath=__
   ```
   PASS iff both paths match expected. Bug is demonstrated when `objectPath` is correct and
   `heapPath` returns ~`0.5` (the raw texel) for both `ref` values.

### 2.3 `compare.comp` (sketch)
Two variants of the sampled type. Simplest: parameterize by a spec constant or just two entry
points / two shader files — one for the classic set, one for the heap mapping — but keep the
**sampling expression identical**:
```glsl
// classic descriptor set version
layout(set=0, binding=0) uniform texture2D    uDepth;
layout(set=0, binding=1) uniform samplerShadow uCmp;
layout(set=0, binding=2) buffer Out { float r[2]; } o;
layout(push_constant) uniform PC { vec2 uv; vec2 refs; } pc;
void main() {
    o.r[0] = texture(sampler2DShadow(uDepth, uCmp), vec3(pc.uv, pc.refs.x));
    o.r[1] = texture(sampler2DShadow(uDepth, uCmp), vec3(pc.uv, pc.refs.y));
}
```
The heap version uses the same source; only the set/binding → heap-offset mapping differs (handled
on the C++ side, exactly like Honey). This mirrors Honey's real shadow path
(`Renderer3D_DeferredLighting.glsl` used `samplerCubeArrayShadow` over a `textureCubeArray` +
`samplerShadow`; a 2D depth is enough to reproduce and is simpler for NVIDIA to read).

### 2.4 Fidelity notes / variants to include
To preempt "you're using it wrong," capture results for these variants in the report:
- **Combined vs separate sampler:** Honey uses *separate* `texture2D` + `samplerShadow` combined
  in-shader. Test that exact form (it's what fails for us). Optionally also test a combined
  `sampler2DShadow` if the heap path supports it.
- **NEAREST vs LINEAR filter** on the comparison sampler — rule out "PCF filtering hides it."
  (With NEAREST you should still get a hard 0/1; if heap path still returns 0.5, compareEnable is
  definitively ignored.)
- **`compareOp` variants** (`LESS`, `GREATER`) — confirm the op field is ignored too, not just the
  enable bit.
- Confirm the **resource (image) descriptor** is correct by also sampling the depth *without*
  comparison through the heap and checking it returns 0.5 — proves the image/view/heap plumbing is
  right and isolates the fault to the sampler's compare state.

### 2.5 Reference code in this repo (cite file:line in the report so NVIDIA sees real-world usage)
- Heap setup / `vkWriteSamplerDescriptorsEXT` call: `vk_descriptor_heap.cpp:225` (`write_sampler`),
  baking at `:241` (`bake_static_samplers`), heap bind `:276` (`bind`).
- Comparison sampler CI: `vk_utils.h:88` (`make_shadow_cmp_sampler_ci`).
- Set/binding → heap-offset mapping (constant offset for static samplers): `vk_descriptor_mapping.cpp:58`.
- Extension + feature enablement: `vk_backend.cpp:1728` (ext check), `:1761`/`:1839`
  (`VkPhysicalDeviceDescriptorHeapFeaturesEXT::descriptorHeap`), `:1732` (maintenance5 dep).
- Real shader workaround (manual compare we fell back to): `Renderer3D_DeferredLighting.glsl:193`
  and `:230`.

---

## 3. Diagnostic data to collect (attach to report)

### 3.1 Re-verify environment (run these; paste output)
```bash
vulkaninfo --summary | grep -iE "deviceName|driverName|driverInfo|driverVersion|apiVersion"
vulkaninfo | grep -i "descriptor_heap"          # confirm extension + revision
cat /proc/driver/nvidia/version                  # driver branch (note: Open module)
echo "$VULKAN_SDK"
uname -a
```

### 3.2 Dump the descriptor-heap properties (helps NVIDIA reproduce alignment)
In the repro, query and print `VkPhysicalDeviceDescriptorHeapPropertiesEXT`
(`samplerDescriptorAlignment`, `samplerHeapAlignment`, the sampler descriptor size from
`vkGetPhysicalDeviceDescriptorSizeEXT(VK_DESCRIPTOR_TYPE_SAMPLER)`, etc.). Include in the report.

### 3.3 Validation layer output
Run the repro with `VK_LAYER_KHRONOS_validation` enabled and `VK_VALIDATION_FEATURE_ENABLE_*` as
desired. Capture stdout/stderr. The expected finding — **no validation errors** — is itself
evidence the API is being used correctly and the fault is in the driver. Note: validation layer was
unavailable in the original debugging sandbox, so running it cleanly here adds real value.

### 3.4 The two output numbers
The pass/fail table from §2.2 is the core evidence. Capture it for each variant in §2.4.

---

## 4. Write the report (template to fill in)

File: `todos/vk_descriptor_heap_compare_bug/REPORT.md` (final artifact). Submit via the
**NVIDIA Vulkan/Developer bug form** or the developer forums (Vulkan section). Sections:

```
Title: VK_EXT_descriptor_heap: samplers written via vkWriteSamplerDescriptorsEXT ignore
       compareEnable/compareOp (Dref returns raw depth) — RTX 3080, driver 595.71.05 (Open)

1. Summary
   One paragraph: heap-written comparison sampler does not perform depth comparison; identical
   VkSampler object does. Spec requires identical behavior.

2. Environment
   Paste the §1 table.

3. Extension / feature state
   VK_EXT_descriptor_heap rev 1 enabled; VkPhysicalDeviceDescriptorHeapFeaturesEXT.descriptorHeap=TRUE;
   VK_KHR_maintenance5 enabled. Paste §3.2 properties.

4. Steps to reproduce
   Build + run the attached repro (link/inline). Exact commands.

5. Expected result
   Both paths return PCF comparison result. ref=0.4 -> 1.0, ref=0.6 -> 0.0 (texel=0.5).

6. Actual result
   Paste the §2.2 table showing heapPath returns ~0.5 for both refs. Include the variant matrix (§2.4).

7. Spec references
   Quote vkWriteSamplerDescriptorsEXT "functions identically" clause. State that the proposal has
   no comparison-sampler carve-out. Links in §6 of this plan.

8. Validation layer
   No errors reported (paste log). Confirms correct API usage.

9. Workaround
   Manual in-shader comparison (sample raw depth with a non-comparison sampler, compute
   ref<=texel). Cite Renderer3D_DeferredLighting.glsl. Note hardware PCF is lost.

10. Attachments
    repro/ (main.cpp, compare.comp, CMakeLists.txt, README.md), vulkaninfo dump, validation log.
```

---

## 5. Submission checklist
- [ ] Repro builds clean with only the Vulkan SDK (no Honey headers) on a fresh checkout.
- [ ] Repro prints the pass/fail table; heap path demonstrably wrong, object path correct.
- [ ] Variant matrix (separate/combined, NEAREST/LINEAR, compareOp) captured.
- [ ] Non-comparison heap sample returns 0.5 (isolates fault to sampler compare state).
- [ ] Validation layer run, no errors, log attached.
- [ ] Environment re-verified (§3.1) right before submission; driver branch noted (Open module).
- [ ] Spec quotes + links included.
- [ ] Report posted to NVIDIA developer bug form / Vulkan dev forum; record the ticket/thread URL
      back into `project_heap_comparison_samplers.md` memory so the workaround can be reverted when fixed.

---

## 6. Reference links
- vkWriteSamplerDescriptorsEXT — https://docs.vulkan.org/refpages/latest/refpages/source/vkWriteSamplerDescriptorsEXT.html
- VK_EXT_descriptor_heap (refpages) — https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_descriptor_heap.html
- VK_EXT_descriptor_heap (proposal) — https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_descriptor_heap.html
- Descriptor Heaps chapter — https://github.khronos.org/Vulkan-Site/spec/latest/chapters/descriptorheaps.html
- Sascha Willems descriptor_heap sample (working reference for heap setup) —
  https://github.com/SaschaWillems/Vulkan/blob/master/examples/descriptorheap/descriptorheap.cpp
- NVIDIA Vulkan driver feedback / bugs — https://developer.nvidia.com/vulkan-driver (and the
  Vulkan section of https://forums.developer.nvidia.com/)