# Shadow Performance Fix

## Prerequisites

This plan assumes the **frame packet refactor** (`frame_packet_refactor.md`) is complete, specifically **Phase 6** of that plan, which migrates `execute_draw` and `execute_dir_draw` to use `ctx.cmd()` directly. The following issues from the original performance analysis are therefore already resolved by that refactor:

- ~~2 blocking `vkWaitForFences` stalls per shadow pass per frame~~ — gone
- ~~Per-frame command buffer allocate/free for shadow work~~ — gone  
- ~~First-frame initial barrier wasted a separate blocking submit~~ — merged into single direct recording pass

What remains after the frame packet refactor:

1. **Shadow pipelines created lazily on first draw** — causes a one-time frame hitch
2. **Cubemap shadows: 6 separate render passes per light** — multiview reduces this to 1
3. **CSM directional: 4 separate render passes** — multiview reduces this to 1
4. **Cascade VP matrices recomputed every frame unconditionally** — can be cached
5. **Directional shadow SSBO uploaded every frame unconditionally** — can skip when unchanged

Items 2 and 3 are the main performance work. Items 1, 4, and 5 are smaller quality-of-life fixes.

---

## Background: Why Multiview

Currently the shadow cubemap loops:
```
for light in shadow_lights:          // up to 8
  for face in 0..5:                  // 6
    vkCmdBeginRenderPass
    draw all meshes
    vkCmdEndRenderPass               // = up to 48 render passes per frame
```

With `VK_KHR_multiview` (part of Vulkan 1.1 core), a render pass can target multiple image layers simultaneously. The mesh shader reads `gl_ViewIndex` to select which VP matrix to use, and the driver/hardware writes each primitive to the correct layer. The loop collapses to:

```
for light in shadow_lights:          // up to 8
  vkCmdBeginRenderPass (6 views)
  draw all meshes                    // shader selects VP via gl_ViewIndex
  vkCmdEndRenderPass                 // = up to 8 render passes per frame
```

This reduces render pass overhead by 6×, and more importantly reduces the number of times each mesh's draw commands are dispatched per frame — from 6× down to 1× per light. On the GPU, multiview is handled at the rasterizer level so it's significantly faster than issuing 6 separate draw calls per mesh per face.

The same applies to directional CSM: 4 passes → 1 pass.

---

## Phase 1 — Enable Multiview and multiviewMeshShader

**File:** `platform/vulkan/vk_backend.cpp` (device creation, around line 1811)

### 1a. Enable the features

`VkPhysicalDeviceVulkan11Features` (`vk11_features`) is already in the pNext chain. Add:

```cpp
vk11_features.multiview = VK_TRUE;
```

`VkPhysicalDeviceMultiviewFeatures` is a subset of Vulkan 1.1 features — enabling via `vk11_features.multiview` is sufficient. No separate struct needed.

For mesh shader + multiview to work together (so the mesh shader can write to multiple views), also change:

```cpp
mesh_features.multiviewMeshShader = VK_TRUE;   // was VK_FALSE
```

### 1b. Add a capability flag

In VulkanContext (or VulkanBackend), add a bool that records whether multiview was successfully enabled, so the shadow module can fall back gracefully on hardware that doesn't support it:

```cpp
// vk_context.h (or vk_backend.h)
bool m_multiview_supported = false;
bool multiview_supported() const { return m_multiview_supported; }
```

Set it during device feature query (before `vkCreateDevice`):

```cpp
// Query support before enabling
VkPhysicalDeviceVulkan11Properties vk11_props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES};
VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
props2.pNext = &vk11_props;
vkGetPhysicalDeviceProperties2(m_physical_device, &props2);
m_multiview_supported = (vk11_props.maxMultiviewViewCount >= 6);
```

> `maxMultiviewViewCount` is guaranteed to be ≥ 6 on any Vulkan 1.1+ device. This check is a safety guard for very unusual hardware.

---

## Phase 2 — Multiview Shadow Cubemap Render Pass and Framebuffer

The shadow cubemap currently uses per-face framebuffers provided by the frame graph (`get_resource_layer_framebuffer("shadowCubemap", layer)`). For multiview, we need:
- A render pass created with `VkRenderPassMultiviewCreateInfo` (6 views)
- One framebuffer per light slot that references all 6 of that light's layers

These are created inside the shadow module itself, not by the frame graph. The frame graph still owns the underlying image and memory — the shadow module just creates additional views and framebuffers on top of it.

### 2a. Create the multiview render pass

Add a helper in `renderer_3d_shadow.cpp` (anonymous namespace, called once from `execute_draw` when the pipeline is first built):

```cpp
static VkRenderPass create_cubemap_multiview_render_pass(VkDevice device) {
    VkAttachmentDescription depth_attachment{};
    depth_attachment.format         = VK_FORMAT_D32_SFLOAT;
    depth_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_attachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 0;
    depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depth_ref;

    // 6 views — one per cubemap face (matches Vulkan spec face order)
    const uint32_t view_mask      = 0b00111111;  // bits 0-5
    const uint32_t correlation_mask = 0b00111111; // all views can run in parallel

    VkRenderPassMultiviewCreateInfo multiview_info{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};
    multiview_info.subpassCount         = 1;
    multiview_info.pViewMasks           = &view_mask;
    multiview_info.correlationMaskCount = 1;
    multiview_info.pCorrelationMasks    = &correlation_mask;

    VkRenderPassCreateInfo rp_ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp_ci.pNext           = &multiview_info;
    rp_ci.attachmentCount = 1;
    rp_ci.pAttachments    = &depth_attachment;
    rp_ci.subpassCount    = 1;
    rp_ci.pSubpasses      = &subpass;

    VkRenderPass rp = VK_NULL_HANDLE;
    VkResult r = vkCreateRenderPass(device, &rp_ci, nullptr, &rp);
    HN_CORE_ASSERT(r == VK_SUCCESS, "create_cubemap_multiview_render_pass failed");
    return rp;
}
```

### 2b. Create per-light-slot multiview framebuffers

For each light slot (0 to `k_max_shadow_lights - 1`), create one framebuffer that covers its 6 layers. These are created once and stored in `ShadowResources`:

```cpp
// Add to ShadowResources struct:
VkRenderPass mv_render_pass = VK_NULL_HANDLE;
VkImageView  mv_layer_views[k_max_shadow_lights] = {};   // one array view per light slot (6 layers each)
VkFramebuffer mv_framebuffers[k_max_shadow_lights] = {}; // one framebuffer per light slot
```

When the shadow resources are first set up (after `ctx.get_resource_vk_image("shadowCubemap")` is available):

```cpp
static void create_multiview_framebuffers(ShadowResources* res, VkImage cubemap_image, VkDevice device) {
    res->mv_render_pass = create_cubemap_multiview_render_pass(device);

    for (uint32_t li = 0; li < k_max_shadow_lights; ++li) {
        // One array image view covering 6 layers for this light slot
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image                           = cubemap_image;
        vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vci.format                          = VK_FORMAT_D32_SFLOAT;
        vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        vci.subresourceRange.baseMipLevel   = 0;
        vci.subresourceRange.levelCount     = 1;
        vci.subresourceRange.baseArrayLayer = li * 6;
        vci.subresourceRange.layerCount     = 6;
        vkCreateImageView(device, &vci, nullptr, &res->mv_layer_views[li]);

        VkFramebufferCreateInfo fb_ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb_ci.renderPass      = res->mv_render_pass;
        fb_ci.attachmentCount = 1;
        fb_ci.pAttachments    = &res->mv_layer_views[li];
        fb_ci.width           = k_shadow_map_resolution;
        fb_ci.height          = k_shadow_map_resolution;
        fb_ci.layers          = 1;   // MUST be 1 when using multiview
        vkCreateFramebuffer(device, &fb_ci, nullptr, &res->mv_framebuffers[li]);
    }
}
```

> ⚠️ With multiview, `VkFramebufferCreateInfo::layers` **must be 1** even though the image view covers 6 layers. This is a Vulkan spec requirement — the multiview layer count is determined by the view mask, not the framebuffer.

### 2c. Clean these up in `invalidate_cubemap_resources()`

```cpp
// Add to invalidate_cubemap_resources():
if (s_res->mv_render_pass != VK_NULL_HANDLE) {
    VkDevice dev = s_res->vk_ctx->get_device();
    for (uint32_t li = 0; li < k_max_shadow_lights; ++li) {
        if (s_res->mv_framebuffers[li]) vkDestroyFramebuffer(dev, s_res->mv_framebuffers[li], nullptr);
        if (s_res->mv_layer_views[li])  vkDestroyImageView(dev, s_res->mv_layer_views[li], nullptr);
        s_res->mv_framebuffers[li] = VK_NULL_HANDLE;
        s_res->mv_layer_views[li]  = VK_NULL_HANDLE;
    }
    vkDestroyRenderPass(dev, s_res->mv_render_pass, nullptr);
    s_res->mv_render_pass = VK_NULL_HANDLE;
}
```

Also call this cleanup in `shutdown()`.

---

## Phase 3 — Update the Cubemap Shadow Pipeline for Multiview

The shadow pipeline is currently created against the frame graph's single-face render pass. It must now be created against the multiview render pass. It also no longer needs `face_index` in the push constant — `gl_ViewIndex` replaces it.

### 3a. Rebuild pipeline against multiview render pass

In `execute_draw`, the lazy pipeline creation block currently does:
```cpp
VkRenderPass shadow_rp = ctx.get_resource_render_pass("shadowCubemap");
```

Replace with:
```cpp
// Use the multiview render pass we created in Phase 2.
VkRenderPass shadow_rp = res->mv_render_pass;
```

No other pipeline spec changes are required — the pipeline itself doesn't need to know about multiview. The render pass association is what tells Vulkan it's a multiview pipeline.

### 3b. Update `Renderer3D_ShadowCubemap.glsl`

**Remove `face_index` from the push constants and use `gl_ViewIndex` instead.**

In the task shader:
```glsl
// Before:
layout(push_constant) uniform PC {
    uint draw_data_base;
    int  light_index;
    uint face_index;
    uint _pad;
} u_PC;

// After:
layout(push_constant) uniform PC {
    uint draw_data_base;
    int  light_index;
} u_PC;
```

In the mesh shader, replace `u_PC.face_index` with `gl_ViewIndex`:
```glsl
// Before:
mat4 mvp = u_Shadow.lights[u_PC.light_index].face_view_proj[u_PC.face_index] * d.model;

// After:
mat4 mvp = u_Shadow.lights[u_PC.light_index].face_view_proj[gl_ViewIndex] * d.model;
```

`gl_ViewIndex` is provided by GLSL extension `GL_EXT_multiview`, which must be declared:
```glsl
#extension GL_EXT_multiview : enable
```

Add this extension declaration at the top of the mesh shader stage.

### 3c. Update `ShadowDrawPC` in `gpu_types.h`

Remove `face_index` from the struct (it's now implicit via `gl_ViewIndex`):

```cpp
// Before:
struct ShadowDrawPC {
    uint32_t draw_data_base;
    int32_t  light_index;
    uint32_t face_index;
    uint32_t _pad;
};

// After:
struct ShadowDrawPC {
    uint32_t draw_data_base;
    int32_t  light_index;
};
```

Update the push constant range size in the pipeline layout creation accordingly.

### 3d. Collapse the face loop in `execute_draw`

**Before (6 render passes per light):**
```cpp
for (uint32_t li = 0; li < res->shadow_light_count; ++li) {
    for (uint32_t fi = 0; fi < 6; ++fi) {
        VkFramebuffer fb = ctx.get_resource_layer_framebuffer("shadowCubemap", li * 6 + fi);
        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        // draw all meshes with PC {draw_data_base, li, fi}
        vkCmdEndRenderPass(cmd);
    }
}
```

**After (1 render pass per light):**
```cpp
for (uint32_t li = 0; li < res->shadow_light_count; ++li) {
    VkFramebuffer fb = res->mv_framebuffers[li];

    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass        = res->mv_render_pass;
    rpbi.framebuffer       = fb;
    rpbi.renderArea.extent = {k_shadow_map_resolution, k_shadow_map_resolution};
    VkClearValue cv{};
    cv.depthStencil        = {1.0f, 0};
    rpbi.clearValueCount   = 1;
    rpbi.pClearValues      = &cv;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    for (const auto& draw : shadow_draw_list) {
        VkDescriptorSet mesh_ds = reinterpret_cast<VkDescriptorSet>(draw.mesh_descriptor_set);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            res->shadow_layout, 1, 1, &mesh_ds, 0, nullptr);

        // No face_index anymore — gl_ViewIndex handles it
        ShadowDrawPC pc{draw.draw_data_base, (int32_t)li};
        vkCmdPushConstants(cmd, res->shadow_layout,
            VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT,
            0, sizeof(ShadowDrawPC), &pc);

        s_fn_draw_mesh_tasks(cmd, draw.meshlet_count, 1, 1);
    }

    vkCmdEndRenderPass(cmd);
}
```

Note: `VK_SHADER_STAGE_FRAGMENT_BIT` is removed from the push constant stage flags — the depth-only shadow pass has no fragment push constant reads.

---

## Phase 4 — Multiview for Directional Shadow (CSM)

Same approach as Phase 3 but for `shadowDirMap` with 4 views (`k_csm_cascade_count = 4`).

### 4a. Create the directional multiview render pass

Add to `DirShadowResources`:
```cpp
VkRenderPass mv_render_pass = VK_NULL_HANDLE;
VkImageView  mv_array_view  = VK_NULL_HANDLE;   // single 4-layer view
VkFramebuffer mv_framebuffer = VK_NULL_HANDLE;  // single framebuffer for all 4 cascades
```

```cpp
static void create_dir_multiview_framebuffer(DirShadowResources* dir, VkImage dir_map_image, VkDevice device) {
    // Render pass: 4 views
    const uint32_t view_mask = (1u << k_csm_cascade_count) - 1u;  // 0b1111 for 4 cascades

    VkAttachmentDescription depth_att{};
    depth_att.format         = VK_FORMAT_D32_SFLOAT;
    depth_att.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth_att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    depth_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_att.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkRenderPassMultiviewCreateInfo mv_info{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};
    mv_info.subpassCount         = 1;
    mv_info.pViewMasks           = &view_mask;
    mv_info.correlationMaskCount = 1;
    mv_info.pCorrelationMasks    = &view_mask;

    VkRenderPassCreateInfo rp_ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp_ci.pNext           = &mv_info;
    rp_ci.attachmentCount = 1;
    rp_ci.pAttachments    = &depth_att;
    rp_ci.subpassCount    = 1;
    rp_ci.pSubpasses      = &subpass;
    vkCreateRenderPass(device, &rp_ci, nullptr, &dir->mv_render_pass);

    // Array view covering all 4 cascade layers
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image                           = dir_map_image;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vci.format                          = VK_FORMAT_D32_SFLOAT;
    vci.subresourceRange                = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, k_csm_cascade_count};
    vkCreateImageView(device, &vci, nullptr, &dir->mv_array_view);

    VkFramebufferCreateInfo fb_ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_ci.renderPass      = dir->mv_render_pass;
    fb_ci.attachmentCount = 1;
    fb_ci.pAttachments    = &dir->mv_array_view;
    fb_ci.width           = k_dir_shadow_map_resolution;
    fb_ci.height          = k_dir_shadow_map_resolution;
    fb_ci.layers          = 1;  // must be 1 with multiview
    vkCreateFramebuffer(device, &fb_ci, nullptr, &dir->mv_framebuffer);
}
```

### 4b. Update `Renderer3D_ShadowDirectional.glsl`

The `face_index` push constant was already noted as "unused" in the shader (it was repurposed to carry the cascade index). Replace it with `gl_ViewIndex`:

```glsl
#extension GL_EXT_multiview : enable

// Remove face_index from push constants entirely:
layout(push_constant) uniform PC {
    uint draw_data_base;
    int  light_index;   // unused for directional, keep for struct parity with ShadowDrawPC or remove
} u_PC;

// In the mesh stage:
mat4 mvp = u_DirShadow.cascade_vp[gl_ViewIndex] * d.model;
```

If `ShadowDrawPC` is already updated in Phase 3 to remove `face_index`, the directional shader just uses the same reduced struct.

### 4c. Collapse the cascade loop in `execute_dir_draw`

**Before (4 render passes):**
```cpp
for (uint32_t c = 0; c < k_csm_cascade_count; ++c) {
    VkFramebuffer fb = ctx.get_resource_layer_framebuffer("shadowDirMap", c);
    vkCmdBeginRenderPass(...);
    // draw with PC {draw_data_base, 0, c, 0}
    vkCmdEndRenderPass(cmd);
}
```

**After (1 render pass):**
```cpp
VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
rpbi.renderPass        = s_dir->mv_render_pass;
rpbi.framebuffer       = s_dir->mv_framebuffer;
rpbi.renderArea.extent = {k_dir_shadow_map_resolution, k_dir_shadow_map_resolution};
VkClearValue cv{};
cv.depthStencil        = {1.0f, 0};
rpbi.clearValueCount   = 1;
rpbi.pClearValues      = &cv;

vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
vkCmdSetViewport(cmd, 0, 1, &viewport);
vkCmdSetScissor(cmd, 0, 1, &scissor);

for (const auto& draw : data->shadow_draw_list) {
    VkDescriptorSet mesh_ds = reinterpret_cast<VkDescriptorSet>(draw.mesh_descriptor_set);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        s_dir->layout, 1, 1, &mesh_ds, 0, nullptr);

    ShadowDrawPC pc{draw.draw_data_base, 0};
    vkCmdPushConstants(cmd, s_dir->layout,
        VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT,
        0, sizeof(ShadowDrawPC), &pc);

    s_fn_draw_mesh_tasks(cmd, draw.meshlet_count, 1, 1);
}

vkCmdEndRenderPass(cmd);
```

### 4d. Clean up in `invalidate_dir_shadow_resources()`

```cpp
if (s_dir->mv_render_pass) {
    VkDevice dev = s_res->vk_ctx->get_device();
    if (s_dir->mv_framebuffer) vkDestroyFramebuffer(dev, s_dir->mv_framebuffer, nullptr);
    if (s_dir->mv_array_view)  vkDestroyImageView(dev, s_dir->mv_array_view, nullptr);
    vkDestroyRenderPass(dev, s_dir->mv_render_pass, nullptr);
    s_dir->mv_framebuffer  = VK_NULL_HANDLE;
    s_dir->mv_array_view   = VK_NULL_HANDLE;
    s_dir->mv_render_pass  = VK_NULL_HANDLE;
}
```

---

## Phase 5 — Pipeline Pre-warming

Currently, both shadow pipelines are created lazily the first time `shadow_light_count > 0`. This causes a frame hitch on the first frame that has shadow-casting lights.

### 5a. Add a pre-warm hook to `Renderer3DShadow`

```cpp
// renderer_3d_shadow.h
static void prewarm_pipelines(FrameGraphPassContext& ctx);
```

```cpp
// renderer_3d_shadow.cpp
void Renderer3DShadow::prewarm_pipelines(FrameGraphPassContext& ctx) {
    if (!s_res || !s_res->vk_ctx) return;

    // Cubemap pipeline
    if (!s_res->shadow_pipeline && s_res->mv_render_pass) {
        void* meshlet_extra_layout = VulkanRendererAPI::get_or_create_meshlet_set_layout();
        auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_ShadowCubemap.glsl");
        // ... same spec setup as the lazy creation path ...
        Ref<Pipeline> pipe = Pipeline::create(spec, s_res->mv_render_pass, meshlet_extra_layout);
        s_res->shadow_pipeline_ref = pipe;
        s_res->shadow_pipeline = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
        s_res->shadow_layout   = reinterpret_cast<VkPipelineLayout>(pipe->get_native_pipeline_layout());
    }

    // Directional pipeline
    if (s_dir && !s_dir->pipeline && s_dir->mv_render_pass) {
        void* meshlet_extra_layout = VulkanRendererAPI::get_or_create_meshlet_set_layout();
        auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_ShadowDirectional.glsl");
        // ... same spec setup as the lazy creation path ...
        Ref<Pipeline> pipe = Pipeline::create(spec, s_dir->mv_render_pass, meshlet_extra_layout);
        s_dir->pipeline_ref = pipe;
        s_dir->pipeline     = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
        s_dir->layout       = reinterpret_cast<VkPipelineLayout>(pipe->get_native_pipeline_layout());
    }
}
```

### 5b. Call pre-warm after frame graph compilation

In `SceneViewportRenderer::rebuild_frame_graph()`, after `m_frame_graph` is assigned, call pre-warm by running a one-time synthetic pass context or by providing a direct API:

```cpp
// scene_viewport_renderer.cpp, in rebuild_frame_graph() after compile:
Renderer3DShadow::prewarm_pipelines_after_rebuild(vk_ctx);
```

Add a version that takes a `VulkanContext*` directly (skipping the `FrameGraphPassContext`) since at this point no frame is in flight:

```cpp
static void prewarm_pipelines_after_rebuild(VulkanContext* ctx);
```

This version creates the multiview framebuffers (if not yet created) and then creates both pipelines.

> The multiview render pass does not depend on the frame graph's render pass (unlike the old code that used `ctx.get_resource_render_pass("shadowCubemap")`). So this can be done immediately after frame graph compilation.

---

## Phase 6 — Cascade VP Caching

`compute_cascade_vp` and `compute_csm_cascades` run every frame even when the camera and light haven't moved. Adding a dirty flag avoids redundant recomputation.

### 6a. Add dirty tracking to `DirShadowResources`

```cpp
struct DirShadowResources {
    // ... existing members ...
    
    // Cache for cascade VP matrices
    bool     cascade_dirty = true;
    glm::vec3 cached_light_dir{0.0f};
    glm::vec3 cached_cam_pos{0.0f};
    float     cached_cam_near = 0.0f;
    float     cached_cam_far  = 0.0f;
    float     cached_cam_fov  = 0.0f;
    float     cached_shadow_dist = 0.0f;
    DirectionalShadowSSBO cached_ssbo{};
};
```

### 6b. Dirty check in `execute_dir_draw`

Before calling `compute_csm_cascades`, compare current values to cached ones:

```cpp
const bool camera_changed =
    data->scene_camera_pos  != s_dir->cached_cam_pos  ||
    data->scene_camera_near != s_dir->cached_cam_near ||
    data->scene_camera_far  != s_dir->cached_cam_far  ||
    data->scene_camera_fov  != s_dir->cached_cam_fov;

const bool light_changed =
    glm::vec3(dir_light.direction) != s_dir->cached_light_dir;

const bool dist_changed =
    data->directional_shadow_distance != s_dir->cached_shadow_dist;

if (s_dir->cascade_dirty || camera_changed || light_changed || dist_changed) {
    compute_csm_cascades(..., ssbo.cascade_vp, ssbo.cascade_splits);
    
    // Update cache
    s_dir->cached_cam_pos     = data->scene_camera_pos;
    s_dir->cached_cam_near    = data->scene_camera_near;
    s_dir->cached_cam_far     = data->scene_camera_far;
    s_dir->cached_cam_fov     = data->scene_camera_fov;
    s_dir->cached_light_dir   = glm::vec3(dir_light.direction);
    s_dir->cached_shadow_dist = data->directional_shadow_distance;
    s_dir->cached_ssbo        = ssbo;
    s_dir->cascade_dirty      = false;
} else {
    ssbo = s_dir->cached_ssbo;
}

vk_ctx->upload_directional_shadows(frame, ssbo);
```

Set `cascade_dirty = true` in `invalidate_dir_shadow_resources()` so the cache is cleared on resize/rebuild.

---

## Files Changed Summary

| File | Phase | Change |
|---|---|---|
| `platform/vulkan/vk_backend.cpp` | 1 | Enable `multiview` + `multiviewMeshShader` features |
| `platform/vulkan/vk_context.h` | 1 | Add `m_multiview_supported`, `multiview_supported()` |
| `Honey/renderer/renderer_3d/renderer_3d_shadow.cpp` | 2–6 | Most of the work — multiview FBs, pipeline pre-warm, cascade cache, collapsed loops |
| `Honey/renderer/renderer_3d/renderer_3d_shadow.h` | 5 | Add `prewarm_pipelines_after_rebuild` declaration |
| `Honey/renderer/gpu_types.h` | 3 | Remove `face_index` from `ShadowDrawPC` |
| `assets/shaders/Renderer3D_ShadowCubemap.glsl` | 3 | Add `GL_EXT_multiview`, use `gl_ViewIndex`, remove `face_index` from PC |
| `assets/shaders/Renderer3D_ShadowDirectional.glsl` | 4 | Add `GL_EXT_multiview`, use `gl_ViewIndex`, remove `face_index` from PC |
| `Honey/renderer/scene_viewport_renderer.cpp` | 5 | Call `prewarm_pipelines_after_rebuild` after frame graph compile |

---

## Performance Impact Summary

| Issue | Before | After |
|---|---|---|
| GPU stalls per frame | 2 (fixed by frame packet refactor) | 0 |
| Render passes per frame (cubemap, 1 light) | 6 | 1 |
| Render passes per frame (cubemap, 8 lights) | 48 | 8 |
| Render passes per frame (CSM) | 4 | 1 |
| Draw dispatches per mesh per frame (cubemap, 1 light) | 6 | 1 |
| Draw dispatches per mesh per frame (CSM) | 4 | 1 |
| Pipeline creation on first shadow frame | hitch | none (pre-warmed) |
| Cascade VP recompute when camera/light static | every frame | skipped |

---

## Verification Checklist

- [ ] **Phase 1**: Validation layer reports no errors about unsupported features. `multiview_supported()` returns true on target hardware.
- [ ] **Phase 2**: `mv_framebuffers` and `mv_render_pass` are created without Vulkan errors. Check with validation layer enabled.
- [ ] **Phase 3**: Cubemap shadows render correctly across all 6 faces. Verify no shadow acne or face orientation issues (the face VP matrices are unchanged; only `gl_ViewIndex` replaces the push constant). Test with a point light and inspect shadow quality from multiple viewing angles.
- [ ] **Phase 4**: CSM cascades render correctly. Check cascade boundaries are watertight. Test with the directional light debug view (the existing cascade visualisation shader) if available.
- [ ] **Phase 5**: No frame hitch on first frame with shadow-casting lights after scene load.
- [ ] **Phase 6**: GPU timestamp query shows lower frame time when camera and directional light are stationary. Verify shadows still update immediately when the camera or light moves.
- [ ] **Regression**: Run `bistro_exterior.hns`, `bistro_interior.hns`, and `gizmo_transform_test.hns`. Point light and directional shadows should look identical to before the refactor.

---

## Risks

**R1: `multiviewMeshShader` support.** Not all Vulkan 1.1 hardware supports multiview with mesh shaders. The `multiview_supported()` flag guards against `maxMultiviewViewCount < 6`, but `multiviewMeshShader` is a separate feature bit. Query `VkPhysicalDeviceMeshShaderFeaturesEXT::multiviewMeshShader` before enabling it and set `m_multiview_supported = false` if absent. In that case the shadow module falls back to the existing per-face/per-cascade loop (the old code path should be kept for one release behind a `if (multiview_supported())` branch).

**R2: Face orientation with `gl_ViewIndex`.**
The current code uses `make_shadow_face_view(pos, face_index)` where the face order matches Vulkan's cubemap face convention. `gl_ViewIndex` counts 0–5 in the order the bits appear in the view mask — which is also 0–5. These match, so no reordering is needed. Verify this with a test scene containing a point light and confirmed shadow directions.

**R3: Layer image barrier scope.**
The image barriers that transition `DEPTH_STENCIL_READ_ONLY → DEPTH_STENCIL_ATTACHMENT` and back currently specify `layerCount = k_max_shadow_lights * 6` (all layers). This remains correct with multiview — the barrier covers all layers regardless of how many views the render pass uses. No change needed here.

**R4: `ShadowDrawPC` size change breaks existing pipeline cache.**
Removing `face_index` changes the push constant size. Invalidate the pipeline cache (`assets/cache/pipelines/vk/`) on first run after this change. The cache is keyed on pipeline state including push constant ranges, so an outdated cache entry would either be ignored (with pipeline recompile) or cause a validation error. Deleting the cache directory before the first run after this refactor is sufficient.