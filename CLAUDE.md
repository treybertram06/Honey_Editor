# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Linux (primary platform)
```bash
Use CLion MCP or ask user to build with CMake
```

### Running
Can run from any working directory. Games are **projects**: portable folders anywhere on disk (`<Name>/{<Name>.honeyproj, Assets/, Scripts/, Cache/}`), never inside this repo. `AssetPaths` (`Honey/engine/src/Honey/core/asset_paths.*`) locates everything at startup and logs which rule matched:
```bash
./cmake-build-debug/Honey_Editor                                # --project <dir> → last-opened project → welcome window
./cmake-build-debug/Honey_Runtime --project ~/Desktop/Sandbox   # or a shipped <exe dir>/game/<Name>.honeyproj; refuses to start without one
```
Three tiers of content, never mixed:
- **Engine content** (shaders, frame graphs, default fonts/textures) in `engine_content/`: `AssetPaths::engine_content_root()` (`--engine-content` → `HONEY_ENGINE_CONTENT` → `<exe dir>/engine_content` → source tree in dev builds), addressed in data with the `engine:` prefix (`engine:fonts/JetBrains_Mono/...`).
- **Editor resources** (icons, editor defaults) in `resources/`, only through `EditorPaths::resource()`.
- **Project assets** under the open project's `Assets/` (`AssetPaths::asset_root()`, empty when no project is open), stored asset-relative (`models/Box/Box.gltf`).

Never hardcode an asset location (no `ASSET_ROOT`, `"../assets"` or absolute paths): use `AssetPaths::resolve()`, and `to_portable`/`from_portable` when writing/reading paths in scene/prefab files (asset-relative or `engine:` on disk, absolute in memory). Per-user data (`settings.yaml`, shader/pipeline caches, `editor_prefs.txt`) lives in `AssetPaths::user_data_dir()` (`config/` in dev builds).

### Windows
```bat
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug --target Honey_Editor
.\build\Debug\Honey_Editor.exe
```

There are no automated tests — validation is done via the test scenes in the **Sandbox** project (`~/Desktop/Sandbox`: glTF corpus, HDRIs, test scenes/prefabs/scripts; unversioned, outside this repo). The sample game is the **FirstGame** project (`~/Desktop/FirstGame`, its own git repo).

**Build prerequisite (C# scripting):** the managed engine assembly is rebuilt via `dotnet build` on every engine build (the `HoneyManaged` CMake target), so a **.NET 10 SDK (`dotnet`) must be on `PATH`**. The .NET *runtime* is vendored/bundled by `Honey/cmake/VendorDotnet.cmake` (pins .NET 10.0.8), so no system runtime install is needed.

## Architecture Overview

**Honey Editor** is a C++20 game engine + editor. The project has two main components:

- `Honey/engine/` — Static engine library (CMake target `engine`)
- `src/` — Editor application (`Honey_Editor`) that links against the engine

### Layered Architecture

```
src/honey_editor_app.cpp       ← Entry point, creates EditorLayer
src/editor_layer.cpp           ← Full editor UI (scene hierarchy, inspector, viewport, gizmos)
src/panels/                    ← SceneHierarchy, ContentBrowser, Viewport, PhysicsDebug, RendererDebug
src/scripting/script_loader    ← C++ native-DLL hot-loading (experimental; separate from the C# path)
    ↓
Honey/engine/src/Honey/        ← Core engine library (target `engine`)
    core/       ← Application lifecycle, layer stack, input, logging (spdlog), UUID, task system (enkiTS)
    events/     ← Event types + dispatch
    renderer/   ← Backend-agnostic renderer API, 2D/3D renderers, cameras, shaders, framebuffers, frame graph
        renderer_3d/  ← Deferred, forward, geometry/meshlet, SSAO, shadows, path tracer
    scene/      ← ECS (EnTT), Entity/Component wrappers, YAML serialization, cloth system
    scripting/  ← C#/.NET host (csharp_script_engine, csharp_script_glue, dotnet_host)
    physics/    ← Jolt 3D physics (engine, contact listener, debug renderer, job system)
    audio/      ← SoLoud audio
    loaders/    ← glTF/model loading (tinygltf)
    math/       ← glm helpers, YAML↔glm conversion
    ui/         ← Editor notifications, ImGui utilities
    utils/      ← Platform utilities (file dialogs, etc.)
    imgui/      ← ImGui integration
    debug/      ← Profiling instrumentation
Honey/engine/src/platform/     ← Per-platform backends (NOTE: sibling of Honey/, not under it)
    vulkan/     ← Primary stable backend (descriptor heap, bindless, mesh shaders, RT)
    opengl/     ← Unstable/maintenance mode (legacy 2D/basic path)
    linux/      ← X11 window + input (primary)
    windows/    ← Win32 window + input
    macos/      ← Cocoa window + input + Obj-C++ file-dialog bridge (experimental)
Honey/managed/HoneyEngine/     ← Managed C# engine assembly (HoneyEngine.dll, net10.0)
```

### Key Abstractions

- **`Ref<T>` / `Scope<T>`** — `shared_ptr` / `unique_ptr` aliases used throughout the engine
- **`RendererAPI`** — Strategy interface for Vulkan vs OpenGL; selected at startup via the per-user `config/settings.yaml`
- **`Layer` / `LayerStack`** — Composable update/render pipeline; `EditorLayer` is the primary layer
- **`Scene` + `Entity`** — Thin wrapper around EnTT's `entt::registry`; entities are just IDs + components
- **`FrameGraph`** — DAG-based render pass management and the **primary per-frame render driver**: every 3D pass (g-buffer, lighting, shadows, SSAO, path tracer) registers as a frame-graph executor and runs through `frame_graph.cpp` each frame. Data-driven — graphs load from `engine_content/frame_graphs/`. Supports compute passes.
- **`ShaderCache`** — Compiles GLSL → SPIR-V via shaderc (incl. mesh/task stages), caches to `config/cache/shaders/` (`AssetPaths::cache_dir()`)

### Renderer Backends

- **Vulkan** (`platform/vulkan/`): Stable and under active development. Targets **Vulkan 1.3** with timeline semaphores and dedicated compute queues. On top of 1.3 it conditionally enables a modern feature set: **`VK_EXT_descriptor_heap` + `VK_KHR_maintenance5`** (the descriptor-heap path, with fallback slots), **bindless via `VK_EXT_descriptor_indexing`**, **mesh shaders (`VK_EXT_mesh_shader`**, the default `GeometryPath: Meshlet`), and **ray tracing** (`VK_KHR_acceleration_structure`/`ray_tracing_pipeline`/…) feeding a path tracer. Render paths (deferred g-buffer + lighting, SSAO, directional CSM + point-light cubemap shadows, meshlet, path tracer) live under `renderer/renderer_3d/`.
  - **Role split:** `VulkanBackend` (`vk_backend.cpp`) owns instance creation, extension/feature selection, logical-device bring-up, the descriptor heap, swapchain, and the render loop. `VulkanContext` (`vk_context.cpp`) owns queues, swapchain sync, timeline semaphores, and per-frame submit.
- **Lite render tier** (`RendererType::lite`, `frame_graphs/lite.hnfg`, `renderer_3d_lite.*`, `shaders/Renderer3D_Lite.glsl`): a **development-only** single-pass forward renderer. It exists solely so *engine* (non-renderer) features can be developed and tested in the editor on a Mac (Apple Silicon via MoltenVK has no `VK_EXT_descriptor_heap` / mesh shaders). It is **never** a shipping or runtime target: `Honey_Runtime` and exported games require the Full tier, and nothing is designed to work under Lite except the editor's dev loop. Also selectable from the renderer debug panel on capable machines. `VulkanBackend::supports_full_renderer()` reports the device tier; `Renderer3D::is_lite()` is true when the device is lite-only **or** Lite is the selected type, and gates every heap/bindless-dependent CPU-side step (environment, tiled lighting, cloth, debug-line path). Lite uses plain descriptor sets in a real `VkPipelineLayout` (`Pipeline::create_layout_mode`), vertex pulling from `GlobalMeshletBuffers`, and draws meshes, debug lines and flat icon discs inside one `lite.scene` executor. No shadows, IBL, SSAO, bloom, Slug icons or cloth.
- **OpenGL** (`platform/opengl/`): Unstable, maintenance mode. Still selectable at runtime, but effectively a legacy 2D/basic path — the advanced paths above are Vulkan-only.

### Scene File Format

Scenes are YAML files (`.hns`) serialized by `SceneSerializer`. Prefabs use `.hnp`. Both live in a project's `Assets/scenes/` and `Assets/prefabs/`. The project file (`.honeyproj`, YAML, parsed by `GameManifest`) holds the name, engine version, start scene, scene list, window settings and the project-owned `Settings:` block.

### Scripting

- **C# / .NET** (primary): The CLR is hosted in-process via nethost/hostfxr (`scripting/dotnet_host.*`), with native↔managed glue in `csharp_script_engine.*` / `csharp_script_glue.*`. The managed engine assembly lives in `Honey/managed/HoneyEngine/` (`HoneyEngine.dll`, `net10.0`). User scripts are **`.cs` files** in the project's `Scripts/` (`UserScripts.csproj` → `UserScripts.dll`; the editor builds it with `-p:HoneyEngineDll=<engine dll>` so the csproj stays portable) that subclass **`EntityScript`**. Callbacks: `OnCreate()`, `OnUpdate(float dt)`, `OnDestroy()`, `OnCollisionBegin(Entity)`, `OnCollisionEnd(Entity)`. (Lua/Sol2 was fully removed — do not reach for `sol::`.)
- **C++ native-DLL scripts**: Experimental, separate path. Editor loads a compiled `.so`/`.dll` (`libHoneyScripts.so`) via `src/scripting/ScriptLoader` and calls its exported `register_all_scripts()`.

### Physics

- **Jolt** (3D): `Honey/engine/src/Honey/physics/` — `physics_engine_3d`, `jolt_contact_listener`, `jolt_debug_renderer`, `jolt_job_system` (vendored `vendor/JoltPhysics`). Drives `RigidbodyComponent` / `ClothComponent`; the editor exposes a `PhysicsDebugPanel`, and physics settings live in the project's `.honeyproj` `Settings: Physics:` block.
- **Box2D** (2D): vendored `vendor/box2d`, linked into the engine.

### Logging

Use the engine macros: `HN_INFO(...)`, `HN_WARN(...)`, `HN_ERROR(...)`, `HN_TRACE(...)`. Backed by spdlog.


### Vulkan Validation Warnings

- Do a web search of the link provided in the validation warning to ensure your understanding of the issue is correct.

## Architecture Invariants

Standing rules that hold across **all** work, not just one feature. If a plan or change would violate one, **stop and flag it** — don't quietly work around it or find a place to park the offending code. This list is meant to grow — add an invariant here the moment a cross-cutting rule causes (or nearly causes) a mistake.

Feature-specific invariants are the durable, project-wide list's counterpart at the plan level: **every large, multi-step plan doc under `todos/` opens with an `## Invariants (hold across every step)` block** stating the cross-cutting rules for that feature, and any sub-plan restates the inherited invariants it must not break. (Sub-plans that drift from a buried invariant are the exact failure this guards against.)

- **Three content tiers, one dependency direction.** Engine content (`engine_content/`) is needed by editor, player and every project; editor resources (`resources/`) are editor-only and never ship; project assets belong to one game. A project may reference engine content (`engine:`); **engine content and engine code never reference a project, and nothing but the editor references editor resources.** *Fitness check:* delete the file and see what breaks — the editor can't start → engine content; only one game breaks → project.
- **The editor must start with no project open** (welcome state). If a change makes that impossible, an engine feature has leaked into project territory.
- **Projects are portable.** Nothing absolute and nothing relative to the engine checkout is ever written into a project (`.honeyproj`, scenes, prefabs, `.csproj`). A project copied to another machine opens in any copy of the editor. Engine-checkout game/test content does not exist: it goes in a project (Sandbox).
- **`VulkanContext` is a renderer-agnostic interface.** It owns only device, queues, swapchain, and synchronization primitives. Renderer-owned resources — the descriptor heap, and anything belonging to the SSAO / deferred / meshlet / shadow / bindless paths — live on `VulkanBackend` or the renderer, **never** as `VulkanContext` members.
- **Descriptor heap mode is the only descriptor model.** All new Vulkan code binds resources through `VK_EXT_descriptor_heap` (`Pipeline::create_heap_mode`, frame-graph descriptor automation, or bindless indices). No new `VkDescriptorSetLayout` / `VkDescriptorPool` / `vkCmdBindDescriptorSets` / `Pipeline::create_layout_mode` code, ever, outside the two allowed exceptions:
  1. **Code inside the Lite tier** (`Renderer3DLite` and what it draws), because Lite exists for hardware without the heap.
  2. **Third-party code we don't control**, e.g. ImGui's Vulkan backend.

  Anything else needs explicit sign-off from the user. If you think a classic path is required, **stop and ask**; don't write it. *Why:* running two descriptor models side by side is the main source of Vulkan confusion in this codebase (heap binding invalidated by classic binds, `frame_graph.cpp` rebind workarounds, duplicated lifetime code). The existing Full-tier classic paths (Renderer2D text/icons, forward pass, classic G-buffer geometry, debug renderer, classic shadow pipeline, `VulkanContext` global/font sets) are **debt to migrate or delete**, not precedent. Tracked in `todos/deferred_renderer/descriptor_heaps.md` → "Classic-descriptor debt".
- **Lite is dev-only and never shapes runtime design.** Lite exists so engine features can be developed on a Mac in the editor; it is never a runtime target. Runtime/player/export code may require the Full tier and must **not** carry Lite fallbacks; `Honey_Runtime` refuses to start on a Lite-only device with a clear error. Engine features (scripting, physics, scenes, audio…) must still work in the editor under Lite, but new *rendering* features don't need Lite support. Within Lite itself: it never touches the descriptor heap, bindless or mesh shaders; heap-mode and layout-mode pipelines are never mixed inside one frame-graph pass; any code that runs in a Lite frame must tolerate `get_descriptor_heap()` / `Renderer::globals()` being null; Full-tier behaviour must not change when adding Lite work.
- **Lite-owned resources (set layouts, pipeline layout, descriptor pools, frame UBO) live inside `Renderer3DLite`**, never on `VulkanContext`.
  - *Fitness check (mechanical):* if a resource is named after a renderer concept (SSAO, deferred, meshlet, shadow, g-buffer, bindless, …), it cannot be a member of `VulkanContext`. A renderer-concept name on a `VulkanContext` field is the smell — catch it on sight.

## Key Files

| File | Purpose |
|------|---------|
| `src/editor_layer.cpp` | Main editor UI (~35 KB) — start here for most editor feature work |
| `src/panels/scene_hierarchy_panel.cpp` | Largest editor panel (~53 KB) — hierarchy, drag/drop, component UI |
| `Honey/engine/src/Honey/renderer/renderer.h` | High-level render API |
| `Honey/engine/src/Honey/renderer/frame_graph.cpp` | Render orchestration — all 3D passes execute here (~82 KB) |
| `Honey/engine/src/Honey/renderer/renderer_3d/` | 3D pipeline paths (deferred, shadows, SSAO, meshlet, path tracer) |
| `Honey/engine/src/Honey/scene/components.h` | All ECS component definitions |
| `Honey/engine/src/Honey/scene/scene.h` | World container (EnTT registry wrapper) |
| `Honey/engine/src/Honey/scene/scene_serializer.cpp` | YAML scene/prefab I/O (`.hns` / `.hnp`) |
| `Honey/engine/src/Honey/scripting/csharp_script_engine.cpp` | C# scripting host (CLR init, per-entity callbacks) |
| `Honey/engine/src/Honey/physics/physics_engine_3d.cpp` | Jolt 3D physics integration |
| `Honey/engine/src/platform/vulkan/vk_context.cpp` | Vulkan queues, swapchain, timeline sync, per-frame submit (~130 KB) |
| `Honey/engine/src/platform/vulkan/vk_backend.cpp` | Vulkan instance/device/extension bring-up + swapchain + render loop (~88 KB) |
| `Honey/engine/src/Honey/core/asset_paths.cpp` | Locates engine content, the open project and per-user data; `resolve`/`to_portable`/`from_portable` |
| `Honey/engine/src/Honey/core/settings.cpp` | Settings persistence; `schema()` is the one table saying which key is per user and which is per project |
| `Honey/engine/src/Honey/core/project_template.cpp` | New Project skeleton (`.honeyproj`, `Assets/`, `Scripts/UserScripts.csproj`, `.gitignore`) |
| `config/settings.yaml` | **Per-user** settings only (renderer backend, `RendererType`, vsync, debug toggles, window placement). Project-owned keys (`GeometryPath`, `DirShadowDistance`, bloom, AA, `Physics:`) live in the `.honeyproj` `Settings:` block |

## Code Conventions

Derived from the existing engine + editor source. **Read a neighboring file before writing** if anything here is ambiguous — match the surrounding code.

**Formatting** is pinned by `.clang-format` (4-space indent, attached braces, left-aligned pointers, no include sorting). Don't hand-format against it; run `clang-format` or let CLion apply it.

**Naming**
- Types (classes, structs, enums): `PascalCase` — `TransformComponent`, `EditorLayer`, `Renderer`
- Functions and variables: `snake_case` — `get_transform()`, `on_update()`, `view_projection_matrix`
- `enum class` values: lowercase `snake_case` — `RendererAPI::API::vulkan`, `SceneState::edit`
- Member prefixes: `m_` instance members, `s_` statics, `g_` globals — `m_window`, `s_instance`, `g_assets_dir`
- Macros: `SCREAMING_CASE` with `HN_` / `HONEY_` prefix — `HN_CORE_ASSERT`, `HONEY_API`

**Idioms**
- Smart pointers: use `Ref<T>` (shared) and `Scope<T>` (unique) with `CreateRef`/`CreateScope`, not raw `std::shared_ptr`/`std::make_unique`
- Logging & asserts: `HN_CORE_INFO`/`HN_CORE_ASSERT` etc. **inside the engine** (`Honey/`), `HN_INFO`/`HN_ASSERT` in the **editor/app** (`src/`)
- Profiling: put `HN_PROFILE_FUNCTION();` at the top of non-trivial functions
- Headers: `#pragma once` (no include guards). `.cpp` files include `hnpch.h` first
- Everything engine-side lives in `namespace Honey { ... }`
- ECS components are plain structs with a defaulted default ctor, a defaulted copy ctor, then converting ctors (see `components.h`)

If you catch yourself inventing a pattern not covered here, stop and read an existing file in the same subsystem first.

## Platform Notes

- **Linux** is the primary development platform (X11; Wayland disabled for ImGui docking branch compatibility)
- **macOS** is experimental, but more built-out than a stub — it has a real Cocoa window/input backend and an Obj-C++ file-dialog bridge (`macos_file_dialog_bridge.mm`)
- Vulkan SDK must be installed and `VULKAN_SDK` env var set (Windows especially)
- `ccache` is detected automatically and recommended on Linux for faster rebuilds