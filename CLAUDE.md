# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Linux (primary platform)
```bash
Use CLion MCP or ask user to build with CMake
```

### Running
Always run from the project root so assets resolve correctly:
```bash
./cmake-build-debug/Honey_Editor
```

### Windows
```bat
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug --target Honey_Editor
.\build\Debug\Honey_Editor.exe
```

There are no automated tests — validation is done via test scenes in `assets/scenes/` and the sandbox `application` target.

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
- **`RendererAPI`** — Strategy interface for Vulkan vs OpenGL; selected at startup via `config/settings.yaml`
- **`Layer` / `LayerStack`** — Composable update/render pipeline; `EditorLayer` is the primary layer
- **`Scene` + `Entity`** — Thin wrapper around EnTT's `entt::registry`; entities are just IDs + components
- **`FrameGraph`** — DAG-based render pass management and the **primary per-frame render driver**: every 3D pass (g-buffer, lighting, shadows, SSAO, path tracer) registers as a frame-graph executor and runs through `frame_graph.cpp` each frame. Data-driven — graphs load from `assets/frame_graphs/`. Supports compute passes.
- **`ShaderCache`** — Compiles GLSL → SPIR-V via shaderc (incl. mesh/task stages), caches to `assets/cache/shaders/`

### Renderer Backends

- **Vulkan** (`platform/vulkan/`): Stable and under active development. Targets **Vulkan 1.3** with timeline semaphores and dedicated compute queues. On top of 1.3 it conditionally enables a modern feature set: **`VK_EXT_descriptor_heap` + `VK_KHR_maintenance5`** (the descriptor-heap path, with fallback slots), **bindless via `VK_EXT_descriptor_indexing`**, **mesh shaders (`VK_EXT_mesh_shader`**, the default `GeometryPath: Meshlet`), and **ray tracing** (`VK_KHR_acceleration_structure`/`ray_tracing_pipeline`/…) feeding a path tracer. Render paths (deferred g-buffer + lighting, SSAO, directional CSM + point-light cubemap shadows, meshlet, path tracer) live under `renderer/renderer_3d/`.
  - **Role split:** `VulkanBackend` (`vk_backend.cpp`) owns instance creation, extension/feature selection, logical-device bring-up, the descriptor heap, swapchain, and the render loop. `VulkanContext` (`vk_context.cpp`) owns queues, swapchain sync, timeline semaphores, and per-frame submit.
- **OpenGL** (`platform/opengl/`): Unstable, maintenance mode. Still selectable at runtime, but effectively a legacy 2D/basic path — the advanced paths above are Vulkan-only.

### Scene File Format

Scenes are YAML files (`.hns`) serialized by `SceneSerializer`. Prefabs use `.hnp`. Both live in `assets/scenes/` and `assets/prefabs/`.

### Scripting

- **C# / .NET** (primary): The CLR is hosted in-process via nethost/hostfxr (`scripting/dotnet_host.*`), with native↔managed glue in `csharp_script_engine.*` / `csharp_script_glue.*`. The managed engine assembly lives in `Honey/managed/HoneyEngine/` (`HoneyEngine.dll`, `net10.0`). User scripts are **`.cs` files** in `assets/scripts/` (`UserScripts.csproj` → `UserScripts.dll`) that subclass **`EntityScript`**. Callbacks: `OnCreate()`, `OnUpdate(float dt)`, `OnDestroy()`, `OnCollisionBegin(Entity)`, `OnCollisionEnd(Entity)`. (Lua/Sol2 was fully removed — do not reach for `sol::`.)
- **C++ native-DLL scripts**: Experimental, separate path. Editor loads a compiled `.so`/`.dll` (`libHoneyScripts.so`) via `src/scripting/ScriptLoader` and calls its exported `register_all_scripts()`.

### Physics

- **Jolt** (3D): `Honey/engine/src/Honey/physics/` — `physics_engine_3d`, `jolt_contact_listener`, `jolt_debug_renderer`, `jolt_job_system` (vendored `vendor/JoltPhysics`). Drives `RigidbodyComponent` / `ClothComponent`; the editor exposes a `PhysicsDebugPanel`, and `settings.yaml` has a `Physics:` block.
- **Box2D** (2D): vendored `vendor/box2d`, linked into the engine.

### Logging

Use the engine macros: `HN_INFO(...)`, `HN_WARN(...)`, `HN_ERROR(...)`, `HN_TRACE(...)`. Backed by spdlog.


### Vulkan Validation Warnings

- Do a web search of the link provided in the validation warning to ensure your understanding of the issue is correct.

## Architecture Invariants

Standing rules that hold across **all** work, not just one feature. If a plan or change would violate one, **stop and flag it** — don't quietly work around it or find a place to park the offending code. This list is meant to grow — add an invariant here the moment a cross-cutting rule causes (or nearly causes) a mistake.

Feature-specific invariants are the durable, project-wide list's counterpart at the plan level: **every large, multi-step plan doc under `todos/` opens with an `## Invariants (hold across every step)` block** stating the cross-cutting rules for that feature, and any sub-plan restates the inherited invariants it must not break. (Sub-plans that drift from a buried invariant are the exact failure this guards against.)

- **`VulkanContext` is a renderer-agnostic interface.** It owns only device, queues, swapchain, and synchronization primitives. Renderer-owned resources — the descriptor heap, and anything belonging to the SSAO / deferred / meshlet / shadow / bindless paths — live on `VulkanBackend` or the renderer, **never** as `VulkanContext` members.
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
| `config/settings.yaml` | Runtime config — renderer backend + knobs (`RendererType`, `GeometryPath`, `DirShadowDistance`, anisotropy…), `Physics:` block, window settings |

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