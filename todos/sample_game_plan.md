# Sample Game Plan — Menu → Lobby → Gameplay → Export

Goal: build a small game (main menu → lobby with a walkable door → gameplay scene) and use it to find and fix what the engine lacks for exporting shippable games. Work scene by scene. Each scene lists the engine features it requires, in dependency order.

## Invariants (hold across every step)

Inherited from CLAUDE.md — a step that would violate one must stop and flag it:
- `VulkanContext` stays renderer-agnostic. The present/blit path for the player must live on `VulkanBackend`/the renderer, not as `VulkanContext` members.
- The Lite tier never touches the descriptor heap, bindless, or mesh shaders. Anything the player runs each frame (present blit, UI draw) must work with `get_descriptor_heap()` / `Renderer::globals()` null. Full-tier behaviour must not change.
- Lite-owned resources live inside `Renderer3DLite`.
- Engine code uses `HN_CORE_*`, editor/app code uses `HN_*`; `Ref`/`Scope`; `HN_PROFILE_FUNCTION()`; `hnpch.h` first in `.cpp`.
- C# is the scripting path. No `sol::`.

Feature-specific:
1. **Editor and player share one runtime path.** Editor "Play" and `Honey_Player` must drive the same scene lifecycle code (`Scene::on_runtime_start` / `on_update_runtime` / `on_runtime_stop` + the new scene-transition service). No behaviour that only works in one of them.
2. **Scene transitions are deferred.** `Scene.Load` only records a request; the swap happens at a safe point between frames, never inside a script callback or physics step.
3. **Game code never uses absolute or source-tree paths.** All new code resolves assets through the runtime asset root (Step 0.3). No new `ASSET_ROOT` / `"../assets"` uses.
4. **The editor must keep working at every step.** The editor remains the primary dev tool; the player is additive.
5. **Native↔managed API additions follow the existing 3-place pattern:** `NativeFunctionTable` (C# `InternalCalls.cs`), `NativeBindings.cs` wrapper, `csharp_script_glue.cpp` table fill. Append to the end of the table so struct layout stays in sync on both sides.
6. **Every step has a "done when" check** exercised by hand in a test scene, since there are no automated tests.

## Phase 0 — Foundation (needed before any scene can run outside the editor)

### 0.1 `Honey_Player` executable
- New CMake target (alongside `Honey_Editor`) linking `engine`, with `RuntimePlayer` `Application` + `RuntimeLayer` (`src/player/` or `Honey/player/`).
- `RuntimeLayer`: load start scene → `Scene::copy`-free direct load → `on_runtime_start()` → each frame `on_update_runtime(ts)` → render via the primary camera.
- Reuse the lifecycle in `src/editor_layer.cpp` (`on_scene_play` ~L865, `on_scene_stop` ~L883) and `Application::run` (`core/engine.cpp`).
- Done when: `Honey_Player` opens a window and runs `assets/scenes/game.hns` with scripts and physics, no ImGui editor chrome.

### 0.2 Present scene output to the swapchain
- Scene currently renders to an offscreen framebuffer shown in an ImGui viewport. Add a fullscreen present/blit of the scene color target into the swapchain pass (the placeholder in `Application::run`: "Any future direct window-space rendering would go here").
- Must work on both Full and Lite tiers; owned by `VulkanBackend`/renderer, not `VulkanContext`.
- Done when: the player shows the 3D scene full-window on macOS (Lite) and on a Full-tier machine, and resizes correctly.

### 0.3 Runtime asset-root resolver
- Replace compile-time `ASSET_ROOT` / `CONFIG_ROOT` (~25 uses: shader cache, pipeline cache `vk_backend.cpp`, BRDF LUT, cloth, renderer_3d_*, `csharp_script_engine.cpp`, `engine.cpp` settings save) and `"../assets"` (`content_browser_panel.cpp`, scripts) with a single `AssetPaths` service: root derived at startup from the executable location, with a dev-mode fallback to the source tree.
- Editor keeps working by using the dev fallback.
- Done when: both executables run with the asset folder moved, and no `ASSET_ROOT` remains in engine runtime code.

### 0.4 Game manifest
- `game.yaml` (start scene, scene list, window title/size/fullscreen, vsync). Loaded by the player. Editor reads the same file for a "Build Settings" view later.
- Done when: the player starts the scene named in the manifest.

## Phase 1 — Scene 1: Main Menu

Needs: a UI, mouse/cursor control, quit, and scene loading.

### 1.1 Scene-transition service (`Scene.Load`)
- Engine-side `SceneManager::request_load(path)`; processed at end of frame by the player/editor-play loop: `on_runtime_stop()` → deserialize via `SceneSerializer` → `on_runtime_start()`.
- Handle the CLR: `on_runtime_stop()` unloads the user-scripts assembly (`csharp_script_engine.cpp`); make sure reload per scene works or avoid unloading on a transition.
- C#: `Scene.Load(string path)` via the 3-place binding.
- Editor Play: transitions work inside play mode and restore the original editor scene on Stop.
- Done when: a test script calls `Scene.Load` on key press and swaps between two scenes repeatedly without leaks or crashes.

### 1.2 Cursor, quit, time bindings
- C#: `Cursor.Visible/Locked` (maps to `Window::set_cursor_captured`), `Application.Quit()` (maps to `Application::quit()`), `Time.DeltaTime`/`Time.Elapsed`.
- The editor currently captures/uncaptures the cursor in `editor_layer.cpp` (~L472, L605, L886). Keep it working.
- Done when: a script can show the cursor, and quit the player.

### 1.3 Screen-space UI
- Recommended sample approach: expose an ImGui-backed immediate-mode UI to scripts (`UI.Begin/Button/Text/End`) drawn in the player's present pass, so the menu does not wait on a full retained-mode UI system. Keep the API narrow so it can later be re-backed by UI components.
- Alternative (later): `UICanvas` / `UIText` / `UIButton` components. Out of scope for the sample.
- Done when: a menu scene shows a title and Play / Quit buttons, with working mouse clicks, at different window sizes.

### 1.4 Menu scene
- `assets/scenes/menu.hns`: background (camera + simple 3D backdrop or skybox), `MenuController.cs` (Play → `Scene.Load("scenes/lobby.hns")`, Quit → `Application.Quit()`).
- Manifest start scene = menu.
- Done when: launching the player lands on the menu; Play loads the (placeholder) lobby; Quit exits.

## Phase 2 — Scene 2: Lobby

Needs: a character that collides with the world, a trigger door, entity identification, and a persistent state between scenes.

### 2.1 Entity identification API
- C#: `Entity.Name` (TagComponent), `Entity.HasTag/Tag` (or just name for now), `Scene.FindEntityByName` already exists. Add `Entity.IsValid` checks on destroyed entities.
- Done when: `OnCollisionBegin(Entity other)` can tell whether `other` is "Player".

### 2.2 Character controller
- Wire Jolt `CharacterVirtual` (or a locked-rotation capsule rigidbody as the minimum) in `physics_engine_3d.cpp`; new `CharacterControllerComponent` (+ serializer, scene copy, inspector UI in `scene_hierarchy_panel`/entity properties panel).
- C#: `CharacterController.Move(Vector3 desiredVelocity)`, `IsGrounded`, jump.
- Replace direct-transform writes in `assets/scripts/FirstPersonController.cs` with the controller.
- Done when: the player walks the lobby, collides with walls and floor, cannot clip through the door while closed.

### 2.3 Trigger volume verification
- Confirm that `is_sensor` bodies receive `OnContactAdded` / `OnContactRemoved` (`jolt_contact_listener.cpp`) against the character body. Fix filtering if not (Jolt sensors need the right object layers and `SetSensorDetectsStatic` settings).
- Done when: a sensor box in front of the door fires `OnCollisionBegin` / `OnCollisionEnd` for the player only.

### 2.4 Persistent game state across scenes
- Expose a `Game.State` key/value store (bool/int/float/string) that survives `Scene.Load` (separate from `Scene::m_scene_state`, which is cleared on start/stop). Bind get/set from C#.
- Done when: a value set in the lobby is readable in the gameplay scene, and reset when returning to the menu via `Game.State.Clear()`.

### 2.5 Lobby scene and door
- `assets/scenes/lobby.hns`: level geometry (glTF), colliders, light, player prefab + camera, door with sensor trigger.
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
- `assets/scenes/arena.hns`: spawn pickups/targets (prefab instantiation), `GameManager.cs` (score, timer, win/lose using `Game.State`), result UI with Return-to-Lobby and Quit.
- Done when: full loop menu → lobby → arena → win/lose → lobby, repeatable without restarting, with no leaks across loads (watch memory, physics bodies, CLR instances).

## Phase 4 — Export / shipping

Do this last: the sample game defines what must ship.

### 4.1 Precompiled shaders
- Ship SPIR-V (offline compile step or pre-warmed cache) instead of runtime shaderc + `assets/cache/shaders/`; keep shaderc for the editor and dev fallback.
- Done when: the player runs from a clean folder with no shader compile step and no write access to the assets dir.

### 4.2 Writable data separation
- Settings, pipeline cache, logs, and profiler output go to a per-user data dir for the player, not the asset dir or source tree (`engine.cpp` saves `settings.yaml` into `ASSET_ROOT/../config`).
- Done when: the player works from a read-only install folder.

### 4.3 Script build for export
- Build `UserScripts.csproj` in Release as part of export; make sure the vendored .NET runtime (`Honey/cmake/VendorDotnet.cmake`) and `HoneyScripting.runtimeconfig.json` lay out relative to the executable and `dotnet_host` resolves them from there.
- Done when: the exported folder runs on a machine without the .NET SDK.

### 4.4 Package target / Export button
- CMake `install`/`package` target and/or an editor "Export Game" menu action that produces `Game/{Honey_Player, assets (only referenced), managed, runtime, game.yaml}`. Use the manifest's scene list plus a dependency walk over scene/prefab references (reuse the serializer's missing-asset reporting from the recent notification work).
- Done when: the exported folder runs the full sample game on macOS (Lite), then on Linux/Windows.

### 4.5 Retrospective
- Record gaps found during the sample in this doc, and promote cross-cutting rules to CLAUDE.md "Architecture Invariants".

## Open questions to settle early
- UI: ImGui-backed immediate mode for the sample (recommended) vs. building real UI components now.
- Character controller: Jolt `CharacterVirtual` (recommended) vs. capsule rigidbody stopgap.
- Do scene transitions keep the CLR assembly loaded (faster, simpler state) or reload per scene?
