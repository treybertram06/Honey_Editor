# Sample Game Plan — Menu → Lobby → Gameplay → Export

Goal: build a small game (main menu → lobby with a walkable door → gameplay scene) and use it to find and fix what the engine lacks for exporting shippable games. Work scene by scene. Each scene lists the engine features it requires, in dependency order.

## Invariants (hold across every step)

Inherited from CLAUDE.md — a step that would violate one must stop and flag it:
- `VulkanContext` stays renderer-agnostic. The present/blit path for the player must live on `VulkanBackend`/the renderer, not as `VulkanContext` members.
- **Descriptor heap mode only.** All new Vulkan code (present pass, script UI, anything else) uses `VK_EXT_descriptor_heap` (`Pipeline::create_heap_mode`, frame-graph automation or bindless indices). No new descriptor sets / pools / `create_layout_mode`. The only exceptions are code inside the Lite tier and third-party code (ImGui's Vulkan backend). If a step seems to need a classic path, stop and ask.
- **Lite is dev-only.** It exists so engine features can be developed in the *editor* on a Mac. The player and exported games require the Full tier and carry no Lite fallbacks. Engine features built here (scene transitions, scripting bindings, character controller, game state, audio) must still work in editor Play under Lite, so they can be tested on the Mac. Player-only rendering (present, player UI) doesn't need to. Lite-owned resources live inside `Renderer3DLite`; Full-tier behaviour must not change.
- Engine code uses `HN_CORE_*`, editor/app code uses `HN_*`; `Ref`/`Scope`; `HN_PROFILE_FUNCTION()`; `hnpch.h` first in `.cpp`.
- C# is the scripting path. No `sol::`.

Feature-specific:
1. **Editor and player share one runtime path.** Editor "Play" and `Honey_Runtime` must drive the same scene lifecycle code (`Scene::on_runtime_start` / `on_update_runtime` / `on_runtime_stop` + the new scene-transition service). No behaviour that only works in one of them.
2. **Scene transitions are deferred.** `Scene.Load` only records a request; the swap happens at a safe point between frames, never inside a script callback or physics step.
3. **Game code never uses absolute or source-tree paths.** All new code resolves assets through `AssetPaths` (Step 0.3; from Phase 1.5 on, project-relative paths plus the `engine:` prefix for engine content). No new `ASSET_ROOT` / `"../assets"` uses.
4. **The editor must keep working at every step.** The editor remains the primary dev tool; the player is additive.
5. **Native↔managed API additions follow the existing 3-place pattern:** `NativeFunctionTable` (C# `InternalCalls.cs`), `NativeBindings.cs` wrapper, `csharp_script_glue.cpp` table fill. Append to the end of the table so struct layout stays in sync on both sides.
6. **Every step has a "done when" check** exercised by hand in a test scene, since there are no automated tests.

## Phase 0 — Foundation (needed before any scene can run outside the editor)

### 0.1 `Honey_Runtime` executable — DONE
- New CMake target (alongside `Honey_Editor`) linking `engine`, with `HoneyRuntime` `Application` + `RuntimeLayer` (`src/runtime/`).
- On a Lite-only device (no `VK_EXT_descriptor_heap`), `Honey_Runtime` refuses to start: clear error ("Honey_Runtime requires the Full renderer tier") and a non-zero exit code. Test gameplay on the Mac through editor Play instead.
- `RuntimeLayer`: load start scene → `Scene::copy`-free direct load → `on_runtime_start()` → each frame `on_update_runtime(ts)` → render via the primary camera.
- Reuse the lifecycle in `src/editor_layer.cpp` (`on_scene_play` ~L865, `on_scene_stop` ~L883) and `Application::run` (`core/engine.cpp`).
- Done when: `Honey_Runtime` opens a window and runs `assets/scenes/game.hns` with scripts and physics, no ImGui editor chrome.

### 0.2 Present scene output to the swapchain — DONE (code-complete; on-screen/sync-validation check by the user)
- Scene currently renders to an offscreen framebuffer shown in an ImGui viewport. Add a fullscreen present/blit of the scene color target into the swapchain pass (the placeholder in `Application::run`: "Any future direct window-space rendering would go here").
- Full tier only, heap-mode: a heap-mode pipeline sampling the scene colour target through a per-frame transient heap block (rewritten each frame, so resizes are safe) with the static `LinearClamp` sampler. Implemented as `PresentPass` + `Renderer::present` + `Layer::on_window_render`; `FramebufferResizeEvent` / `Window::get_framebuffer_size()` drive resizing. Re-bind the heap before drawing, since earlier classic binds invalidate it. Owned by the renderer (`PresentPass`), not `VulkanContext`. ImGui (classic sets, third-party) draws on top in the same swapchain pass.
- Resizes follow framebuffer *pixel* size (HiDPI), applied deferred at the start of the next update.
- Done when: the player shows the 3D scene full-window on a Full-tier machine, resizes correctly, and is clean under sync validation.

### 0.3 Runtime asset-root resolver — DONE
- Replace compile-time `ASSET_ROOT` / `CONFIG_ROOT` (~25 uses: shader cache, pipeline cache `vk_backend.cpp`, BRDF LUT, cloth, renderer_3d_*, `csharp_script_engine.cpp`, `engine.cpp` settings save) and `"../assets"` (`content_browser_panel.cpp`, scripts) with a single `AssetPaths` service: root derived at startup from the executable location, with a dev-mode fallback to the source tree.
- Editor keeps working by using the dev fallback.
- Done when: both executables run with the asset folder moved, and no `ASSET_ROOT` remains in engine runtime code.

### 0.4 Game manifest — DONE (code-complete; needs a build + hand test)
- *Superseded in 1.5:* the manifest is now the project's `<Name>.honeyproj` (same Game/Window schema plus `Project:` and `Settings:`); `game.yaml`, `--manifest` and `resources/defaults/game.yaml` are gone. Text below is historical.
- `game.yaml` (start scene, scene list, window title/size/mode, vsync). Loaded by the player. Editor reads the same file for a "Build Settings" view later.
- Lives at `<asset root>/../game.yaml`, or `--manifest <file>`. `AssetPaths` only *locates* it (`manifest_path()`); `GameManifest::load` parses/validates. Player loads it in `create_application()` (null → exit 1); the editor never loads it, it only copies `resources/defaults/game.yaml` into place if missing.
- `GameManifest::apply_to` runs in `Application`'s ctor before window creation, overriding window/vsync. `Application::set_persist_settings(false)` in the player so it never writes `settings.yaml`.
- `WindowSettings::mode` (`windowed | maximized | fullscreen`) replaces the `fullscreen` bool (`settings.yaml` still stores `Fullscreen: bool` = maximized). Real fullscreen (borderless at the monitor's video mode) is implemented for the Linux and Windows Vulkan window paths; the Windows backend still ignores maximize/position.
- Done when: the player starts the scene named in the manifest.

## Phase 1 — Scene 1: Main Menu

Needs: a UI, mouse/cursor control, quit, and scene loading.

### 1.1 Scene-transition service (`Scene.Load`) — DONE
- Engine-side `SceneManager::request_load(path)`; processed at end of frame by the player/editor-play loop: `on_runtime_stop()` → deserialize via `SceneSerializer` → `on_runtime_start()`.
- Handle the CLR: `on_runtime_stop()` unloads the user-scripts assembly (`csharp_script_engine.cpp`); make sure reload per scene works or avoid unloading on a transition.
- C#: `Scene.Load(string path)` via the 3-place binding.
- Editor Play: transitions work inside play mode and restore the original editor scene on Stop.
- Done when: a test script calls `Scene.Load` on key press and swaps between two scenes repeatedly without leaks or crashes.

**Implemented** (`scene/scene_manager.{h,cpp}`):
- `request_load` resolves the asset-relative path via `AssetPaths::resolve` and stores it in a single pending slot (last request wins, with a warning). `take_pending` / `clear_pending` / `has_pending` manage it.
- `load_scene(path)` is the one shared `.hns` loader (the player's old `open_scene` now delegates to it) and logs the serializer's load report.
- `perform_pending_load(current, w, h)` does the swap: load new scene first (on failure the old scene keeps running) → `on_runtime_stop` → `wait_idle` → `on_viewport_resize` → `on_runtime_start`. Called by `RuntimeLayer::on_update` (after the quit check; skips rendering that frame) and by `EditorLayer` in `SceneState::play` only. The editor swaps only `m_active_scene`, so Stop restores `m_editor_scene` untouched.
- Stale requests are cleared at the start of the player, at the start of editor play/simulate, and after `on_runtime_stop` in `on_scene_stop` (so a `Scene.Load` made from `OnDestroy` can't leak into the next session).
- Binding: `Scene_Load` appended to the end of the C++ `NativeFunctionTable` struct, the C++ initializer, and the C# struct, plus `NativeBindings.Scene_Load` and `Scene.Load`. Note that this is really four places (the C++ side declares its own mirror struct).
- Decision: the CLR assembly is **reloaded per scene** (stop → start already does this); no keep-loaded path.

**Known caveats / follow-ups:**
- `AudioSystem::init/shutdown` run per scene, so audio is cut at every transition. Revisit in 3.2 (music across scenes).
- The cursor reset after a transition is duplicated in `EditorLayer` and `RuntimeLayer`; it would be safer inside `perform_pending_load`.
- `Scene.Load` in `simulate` mode is not consumed (the request is cleared on Stop). Fine for now.
- Test script `SceneLoadTest.cs` (Sandbox project, `Scripts/`) only loads one way; a ping-pong pair is needed for a long repeated-swap leak check (watch RSS and Jolt body count).

### 1.2 Cursor, quit, time bindings - DONE
- C#: `Cursor.Visible/Locked` (maps to `Window::set_cursor_captured`), `Application.Quit()` (maps to `Application::quit()`), `Time.DeltaTime`/`Time.Elapsed`.
- The editor currently captures/uncaptures the cursor in `editor_layer.cpp` (~L472, L605, L886). Keep it working.
- Done when: a script can show the cursor, and quit the player.

### 1.3 Screen-space UI (DONE — see `phase_1_3_ui.md`)
- Recommended sample approach: expose an ImGui-backed immediate-mode UI to scripts (`UI.Begin/Button/Text/End`) drawn in the swapchain pass after the present. It's ImGui-backed, so it uses ImGui's own descriptors (the allowed third-party exception), and script UI also works in editor Play under Lite. The menu then doesn't have to wait for a full retained-mode UI system. Keep the API narrow so it can later be re-backed by UI components.
- Alternative (later): `UICanvas` / `UIText` / `UIButton` components. Out of scope for the sample.
- Done when: a menu scene shows a title and Play / Quit buttons, with working mouse clicks, at different window sizes.

### 1.5 Projects — separate engine content, editor resources and game projects — DONE
- Introduced three tiers: **engine content** (`engine_content/`: shaders, frame graphs, default fonts/textures), **editor resources** (`resources/`), and portable **projects** (`<anywhere>/<Name>/{<Name>.honeyproj, Assets/, Scripts/, Cache/}`) opened via `--project` or the editor. The 4.6 GB model corpus, HDRIs and test scenes moved to the `Sandbox` project (`~/Desktop/Sandbox`); the real game is `FirstGame` (`~/Desktop/FirstGame`). **From here on, every scene/script path in this plan is relative to the FirstGame project** (`Assets/…` for content, `Scripts/` for C#) unless it says Sandbox.
- Full plan, invariants and steps: `phase_1_5_projects.md`.
- Done when: `assets/` is gone from the engine repo, the editor opens Sandbox and FirstGame by path (and starts with no project), and `Honey_Runtime --project <path>` runs a project copied to another folder.

### 1.4 Menu scene
- FirstGame `scenes/menu.hns`: background (camera + simple 3D backdrop or skybox), `Scripts/MenuController.cs` (Play → `Scene.Load("scenes/lobby.hns")`, Quit → `Application.Quit()`).
- Manifest start scene = menu.
- Done when: launching the player lands on the menu; Play loads the (placeholder) lobby; Quit exits.

## Phase 2 — Scene 2: Lobby

Needs: a character that collides with the world, a trigger door, entity identification, and a persistent state between scenes.

### 2.1 Entity identification API - DONE (code-complete; collision half pending 2.3)
- C#: `Entity.Name` (TagComponent), `Entity.HasTag/Tag` (or just name for now), `Scene.FindEntityByName` already exists. Add `Entity.IsValid` checks on destroyed entities.
- Done when: `OnCollisionBegin(Entity other)` can tell whether `other` is "Player".
- Implemented: `Entity.IsValid`, `Entity.Name` get/set, `Entity.HasName(string)`, `==`/`!=`/`IEquatable`, `ToString()`. New native table entries `Entity_IsValid`, `Entity_GetName`, `Entity_SetName` (appended after `Scene_Load`). Transform glue is guarded against destroyed entities (getters return zeros, setters no-op). Details: `step_2_1_entity_identification.md`.
- Verified: `EntityIdentityTest.hns` + `EntityIdentityTest.cs` (throwaway test files in the old stop-gap `assets/`; no longer present), 16/16 checks PASS (invalid handle, lookup/name, equality, rename round trip, 400-char name, destroyed-entity validity/name/transform).
- Not yet verified: `OnCollisionBegin` with `other.HasName("Player")` in a real collision. The test scene has no rigidbodies, and 3D contact dispatch to scripts is checked in 2.3. Re-run the test with two rigidbodies once 2.3 lands.
- Decision: `HasTag`/`Tag` skipped; name only, for now.

### 2.2 Character controller
- Wire Jolt `CharacterVirtual` (or a locked-rotation capsule rigidbody as the minimum) in `physics_engine_3d.cpp`; new `CharacterControllerComponent` (+ serializer, scene copy, inspector UI in `scene_hierarchy_panel`/entity properties panel).
- C#: `CharacterController.Move(Vector3 desiredVelocity)`, `IsGrounded`, jump.
- Replace direct-transform writes in `FirstPersonController.cs` (Sandbox `Scripts/`; copy into FirstGame when the lobby needs it) with the controller.
- Done when: the player walks the lobby, collides with walls and floor, cannot clip through the door while closed.

### 2.3 Trigger volume verification
- Confirm that `is_sensor` bodies receive `OnContactAdded` / `OnContactRemoved` (`jolt_contact_listener.cpp`) against the character body. Fix filtering if not (Jolt sensors need the right object layers and `SetSensorDetectsStatic` settings).
- Done when: a sensor box in front of the door fires `OnCollisionBegin` / `OnCollisionEnd` for the player only.

### 2.4 Persistent game state across scenes
- Expose a `Game.State` key/value store (bool/int/float/string) that survives `Scene.Load` (separate from `Scene::m_scene_state`, which is cleared on start/stop). Bind get/set from C#.
- Done when: a value set in the lobby is readable in the gameplay scene, and reset when returning to the menu via `Game.State.Clear()`.

### 2.5 Lobby scene and door
- FirstGame `scenes/lobby.hns`: level geometry (glTF), colliders, light, player prefab + camera, door with sensor trigger.
- `DoorTrigger.cs`: on player enter, show a prompt (UI from 1.3), on interact key `Scene.Load("scenes/arena.hns")`. Optionally animate the door via transform.
- Done when: menu → lobby → walk to door → press key → gameplay scene loads; Esc returns to the menu.

## Phase 3 — Scene 3: Gameplay

Needs: gameplay interaction, feedback (audio, text), and win/lose flow. The exact game is a placeholder: collect-N-pickups or shoot-N-targets, with a score and a return-to-lobby condition. `BulletController.cs` and prefab instantiation already exist.

### 3.1 Script-to-component access
- C#: `Entity.Destroy` exists; add `Entity.SetActive/Enabled` (hide/disable), `TextRenderer.Text` setter (score), `MeshRenderer` visibility/color if needed.
- Script-to-script: `Entity.GetScript<T>()` (or events/messages) so a pickup can notify a game manager.
- Done when: a script updates an in-world or UI score from collision events.

### 3.2 Audio from scripts
- C#: `Audio.Play(path, volume)` one-shot, plus `AudioSource` play/stop. Wraps `AudioSystem::create_source/play/stop` (`audio_system.h`).
- Done when: pickup and door sounds play, and music loops across the scene.

### 3.3 Physics queries
- C#: `Physics.Raycast(origin, dir, maxDist, out Hit)` for shooting/interaction. Add to `physics_engine_3d`.
- Done when: a hitscan or interact-ray hits the expected entity.

### 3.4 Extended script properties
- Add `int` and `Vector3` (and string path) to `ScriptComponent::property_overrides`, with inspector and serialization, so values like target scene, spawn counts and speeds are tunable in the editor.
- Done when: `DoorTrigger.TargetScene` is set in the inspector and honoured by the player.

### 3.5 Gameplay scene and loop
- FirstGame `scenes/arena.hns`: spawn pickups/targets (prefab instantiation), `Scripts/GameManager.cs` (score, timer, win/lose using `Game.State`), result UI with Return-to-Lobby and Quit.
- Done when: full loop menu → lobby → arena → win/lose → lobby, repeatable without restarting, with no leaks across loads (watch memory, physics bodies, CLR instances).

## Phase 4 — Export / shipping

Do this last: the sample game defines what must ship.

### 4.1 Precompiled shaders
- Ship SPIR-V (offline compile step or pre-warmed cache) instead of runtime shaderc + the per-user shader cache (`user_data_dir()/cache/shaders/`); keep shaderc for the editor and dev fallback.
- Done when: the player runs from a clean folder with no shader compile step and no write access to the install or project folder.

### 4.2 Writable data separation
- Settings, pipeline cache, logs, and profiler output go to a per-user data dir for the player, not the install folder or source tree. Since 1.5 everything per-user already goes through `AssetPaths::user_data_dir()` (`config/` next to the executable or source tree); what's left is pointing it at a real OS per-user location (`$XDG_DATA_HOME`/`%APPDATA%`) for shipped builds. Project-owned settings already live in the `.honeyproj`, so the player only writes user settings (and `set_persist_settings(false)` keeps it from writing at all).
- Done when: the player works from a read-only install folder.

### 4.3 Script build for export
- Build `UserScripts.csproj` in Release as part of export; make sure the vendored .NET runtime (`Honey/cmake/VendorDotnet.cmake`) and `HoneyScripting.runtimeconfig.json` lay out relative to the executable and `dotnet_host` resolves them from there.
- Done when: the exported folder runs on a machine without the .NET SDK.

### 4.4 Package target / Export button
- CMake `install`/`package` target and/or an editor "Export Game" menu action that produces `<Out>/{Honey_Runtime, engine_content/, managed/ (HoneyEngine.dll + the project's Release UserScripts.dll), runtime/, game/<Name>.honeyproj + game/Assets/ (only referenced assets)}`, the shipped layout `AssetPaths` already resolves. Use the project's scene list plus a dependency walk over scene/prefab references (reuse the serializer's missing-asset reporting from the recent notification work).
- Done when: the exported folder runs the full sample game on Linux, then Windows. macOS is not an export target for this plan: Lite never ships, and macOS export only becomes relevant once it supports the Full tier and there's a real need, well after a game has shipped.

### 4.5 Retrospective
- Record gaps found during the sample in this doc, and promote cross-cutting rules to CLAUDE.md "Architecture Invariants".

## Open questions to settle early
- UI: ImGui-backed immediate mode for the sample (recommended) vs. building real UI components now.
- Character controller: Jolt `CharacterVirtual` (recommended) vs. capsule rigidbody stopgap.
- Do scene transitions keep the CLR assembly loaded (faster, simpler state) or reload per scene?

## Decisions log (answers from the user, 2026-10-13)
- **Lite tier purpose:** development-only, so engine (non-renderer) features can be built and tested in the editor on a MacBook. Never used in production or in any runtime.
- **Descriptor model:** heap mode only. Non-heap descriptors are allowed only inside the Lite tier and in third-party code we don't control (ImGui). Anything else needs explicit sign-off. Reason: running two descriptor models side by side is too confusing to maintain.
- **Existing Full-tier classic paths** (Renderer2D text/icons, forward pass, classic G-buffer geometry, debug renderer, classic shadow pipeline, `VulkanContext` global/font sets): treat as debt to migrate or delete. Tracked in `todos/deferred_renderer/descriptor_heaps.md` → "Classic-descriptor debt".
- **`Honey_Runtime` on a Lite-only device:** refuses to start with a clear error and a non-zero exit code.
- **macOS as an export target:** not for now. Revisit only after a game has shipped and macOS support is a real need (which would also require the Full tier on macOS).
- **Executable name:** `Honey_Runtime` (this doc originally said `Honey_Player`).
- **Project organization (2026-11-17):** three tiers — engine content / editor resources / project. Projects are portable folders anywhere on disk, opened by path; the model/HDRI corpus moves to a `Sandbox` project outside the engine repo; hardware settings stay per user, look-and-feel settings are per project. Done as Phase 1.5, before the menu scene and Phase 2.
