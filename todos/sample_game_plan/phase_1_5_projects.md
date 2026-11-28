# Phase 1.5 — Projects (engine content / editor resources / project separation)

Parent: `sample_game_plan.md` → Phase 1.5. Do this **before the 1.4 menu scene and before Phase 2**, so every scene and script of the first game is born inside a project instead of the global `assets/` dump. Goal: a game is a self-contained, portable folder anywhere on disk; the editor and player open it by path; nothing a game owns lives in the engine repo.

## Invariants (hold across every step)

- **Three tiers, one dependency direction.** *Engine content* (shaders, frame graphs, default fonts/textures, built-in meshes) is needed by editor, player and every project. *Editor resources* (icons, editor ImGui defaults) are editor-only and never ship. *Project* content belongs to one game. A project may reference engine content; **engine content never references a project; nothing but the editor references editor resources.**
- **Mechanical check for "which tier?":** delete it and see what breaks. Editor can't start → engine content. Only one game breaks → project.
- **The editor must start with no project open** (welcome/empty state). If a change makes that impossible, an engine feature has leaked into project territory.
- **Projects are portable.** Nothing absolute and nothing relative-to-the-engine-checkout is ever written to disk in a project: no absolute paths in `.honeyproj`, scenes, prefabs, `.csproj`, or `.gitignore`d generated files that the project depends on. A project downloaded from the internet must open in any copy of Honey_Editor.
- **Every asset path is unambiguous about its tier.** Project-relative by default (`models/Box/Box.gltf`); engine content only via the `engine:` prefix (`engine:fonts/JetBrains_Mono/...`). Never guess by probing both.
- **Game code never uses absolute or source-tree paths** (inherited, sample-plan invariant 3) — still resolves through `AssetPaths`; the dev source-tree fallback applies to *engine content and editor resources only*, never to a project.
- **Editor and player share one resolution path.** Both resolve engine content and project assets through the same `AssetPaths` API; only *how the roots are located* differs (`--project` vs shipped layout).
- **The editor keeps working at every step.** Until step 5 the old `assets/` folder acts as a stop-gap project.
- Naming/idioms per CLAUDE.md. Each step has a hand-test "done when".

## Decisions (2026-11-17)

- Games live in **separate folders anywhere on disk**, opened via `--project <path>` / the editor's Open Project. Not under this repo.
- The 4.6 GB glTF corpus, 300 MB HDRIs, test scenes etc. move to a **`Sandbox` project** outside the engine repo. No more game/test content in the engine repo.
- Hardware-dependent settings (renderer backend, Lite tier, vsync, window placement) are **per user**; look-and-feel and game settings (shadow distance, geometry path, physics, window title/mode, start scene) are **per project**.
- Terminology: say **engine content** for shaders etc., **editor resources** only for `resources/`, **assets** only for project content.

## Target layout

```
Honey_Editor/                 (engine repo)
  engine_content/             shaders/  frame_graphs/  fonts/  textures/ (defaults: BRDF LUT, white, etc.)  meshes/ (built-ins)
  resources/                  editor icons, defaults/imgui.ini        (unchanged role)
  Honey/ src/ ...

<anywhere>/FirstGame/         (a project; its own git repo)
  FirstGame.honeyproj         name, engine_version, start scene, scene list, window, settings overrides
  Assets/                     scenes/ prefabs/ models/ textures/ audio/ fonts/ shaders/ (custom)
  Scripts/                    UserScripts.csproj + *.cs
  Cache/                      derived data (gitignored)
```

Per-user data (settings, shader/pipeline caches, `imgui.ini`, recent-projects list) lives in `user_data_dir()`, never in the project or the engine repo.

## Step 0 — Inventory (read-only, ~1 hr)
Classify every `AssetPaths::asset_root()` / `resolve()` / hardcoded `"shaders/"`, `"fonts/"`, `"frame_graphs/"`, `"cache/"`, `"scripts/"`, `"scenes/"` use as engine content, project, or user data. Output: a table at the bottom of this file. Also list what in `assets/` is needed by the engine itself to run (default environment/HDRI, BRDF LUT, fonts, icon discs) so step 1 moves exactly that. Check `config/settings.yaml` key by key (per user vs per project) and what `game.yaml` / `resources/defaults/game.yaml` become. Done when: no call site is unclassified.

## Step 1 — Split out engine content ✅ implemented, awaiting hand-test
Implemented: `engine_content/{shaders,frame_graphs,fonts/JetBrains_Mono,textures/missing.png}` (git mv); `AssetPaths::engine_content_root()` (`--engine-content` → `HONEY_ENGINE_CONTENT` → `<exe>/engine_content` → dev source tree; marker = `shaders/`+`frame_graphs/`); `engine:` prefix handled by `resolve`/`from_portable`/`to_portable`; all shader/frame-graph/font call sites switched; `cache_dir()` = `user_data_dir()/cache` (`config/cache`); asset-root marker relaxed to "is a directory". Not done: `config/cache` is not gitignored (repo has no .gitignore); no default sky needed.
- Create `engine_content/` and move shaders, frame graphs, default fonts, and the small default textures/environment the renderer requires. Large sample HDRIs/textures are **not** engine content (they go to Sandbox in step 5); the engine gets a tiny default sky if it needs one.
- `AssetPaths::engine_content_root()` (located like today's asset root: `--engine-content`/env/`<exe>/engine_content`/dev source tree) plus `resolve("engine:...")`. Shader compiler, frame-graph loader, `imgui_layer.cpp` fonts, BRDF LUT, `renderer_*` switch to it.
- Shipped layout: `<exe>/engine_content/`. Shader/pipeline caches move to `user_data_dir()/cache` (engine shaders compile identically for every project).
- The old `assets/` remains as the stop-gap project; everything else in it stays put for now.
- Done when: both executables run with `engine_content/` moved elsewhere (via flag) and no shader/font/frame-graph path references `asset_root()`.

## Step 2 — Project root + project file ✅ implemented (minimal), awaiting hand-test
Implemented: `--project <folder|file.honeyproj>` (or shipped `<exe>/game/*.honeyproj`) in `AssetPaths` → `project_root()`, `scripts_dir()`, `has_project()`; asset root = `<project>/Assets`; the `.honeyproj` is the manifest (same Game/Window schema as `game.yaml` plus optional `Project: EngineVersion`, mismatch warns). Per-user `config/` no longer derives from the asset root. `build_and_reload` builds the project's `Scripts/` with `-p:HoneyEngineDll=`; `UserScripts.csproj` references `$(HoneyEngineDll)` (stop-gap default for hand builds). Editor no longer auto-creates a manifest. **Deliberately deferred:** no separate `Project` class (AssetPaths + GameManifest cover it until step 3 needs mutation/New Project); legacy layout (`assets/` + `game.yaml`) still works when no `--project` is given; `to_portable` stays asset-root-relative (which is project-relative when a project is open); `Cache/` and `settings` overrides come in step 4.
- `Project` service (engine): loads/validates `<name>.honeyproj` (YAML), exposes `root()`, `assets_root()`, `scripts_dir()`, `cache_dir()`, name, start scene, scenes, settings overrides, `engine_version`.
- `AssetPaths::asset_root()` becomes `project_assets_root()`; `to_portable`/`from_portable` are project-relative, and `engine:` paths pass through to engine content. Keep accepting legacy `../assets/...` strings so old scenes load until step 5 rewrites them.
- `game.yaml` is absorbed into `.honeyproj` (start scene, scene list, window, vsync); `GameManifest` reads from the project. Player: `--project <path>` or shipped layout `<exe>/game/<name>.honeyproj`.
- `engine_version` mismatch → warning (not error) with a clear message; unknown future fields ignored.
- Done when: `Honey_Runtime --project <path>` runs a project from any folder, and a project folder copied to another machine's engine checkout still loads.

## Step 3 — Editor project workflow ✅ implemented, awaiting hand-test
Implemented: `AssetPaths::Mode {player, editor}` (each app defines `asset_paths_mode()` beside `create_application()`); editor mode may start with no project (only an explicit `--assets` still selects the legacy layout). `AssetPaths::open_project/close_project/has_asset_root` switch projects at runtime (main thread; `resolve`/`to_portable`/`from_portable` are safe with no project). Editor startup: `--project` → last project (`user_data_dir()/editor_prefs.txt`, `EditorPrefs`) → welcome window (Open / New / Recent). File menu: Open/New/Recent/Close Project; scene ops, Play and Simulate are disabled with no project. Switches are deferred to the top of the next `on_update` and confirm when the scene has unsaved changes. Opening a project: manifest validated (bad manifest rolls back to the previous project), `CSharpScriptEngine::on_project_changed()` retargets `UserScripts.dll`, scripts rebuild if stale (`scripts_are_stale()`: dll missing, or older than any `.cs/.csproj` or than `HoneyEngine.dll`), fresh scene, start scene opened, title = "Honey Editor - <name>" (`Window::set_title`). `ProjectTemplate::create` generates the skeleton (`.honeyproj`, `Assets/{scenes,prefabs,models,textures,audio,fonts}`, `Scripts/UserScripts.csproj`, `.gitignore`, empty `scenes/main.hns`). Content browser re-roots on project change. Not done: read-only "Engine" browse root; folder-picker dialog (New Project uses a typed Location); native-script (`libHoneyScripts.so`) path still uses the experimental `scripts/` lookup.
- Startup: `--project <path>` → else last-opened project (user prefs) → else **welcome state** (Open / New / Recent). Editor works with no project (panels empty, play disabled).
- Content browser rooted at `Assets/`; optional read-only "Engine" root for browsing engine content. Open/Save scene, prefab and model dialogs, drag/drop, thumbnails, `to_portable` all go through the project root. Window title shows project name.
- **Scripts:** the project's `Scripts/UserScripts.csproj` references `HoneyEngine.dll` through an MSBuild property (`$(HoneyEngineDll)`), supplied by the editor at build time (`dotnet build -p:HoneyEngineDll=<engine's dll>`) — no HintPath into the engine checkout, so the csproj is portable. The editor builds scripts on project open and on demand, and `dotnet_host` loads `UserScripts.dll` from the project's output. Rebuild is also forced when the engine's managed API changes (we keep hitting `MissingMethodException` otherwise).
- New Project: generates the folder skeleton, `.honeyproj`, `Scripts/UserScripts.csproj`, `.gitignore` (`Cache/`, `bin/`, `obj/`).
- Done when: open project A, then project B, from the editor with no restart issues; a scene saved in one is relative-pathed and loads from a copied folder.

## Step 4 — Settings split ✅ implemented, awaiting hand-test
Implemented: table-driven schema in `settings.cpp` (`schema()`, one `SettingDesc` row per persisted key, owner `user` or `project`); `Settings::get()` and the struct hierarchy are unchanged. API: `load_user/save_user` (user rows only; save drops stale project keys from `settings.yaml`), `load_project(node)/reset_project/save_project(honeyproj)/project_dirty()`. **No cross-file fallback:** project keys come from the `.honeyproj` `Settings:` block (same section/key layout as `settings.yaml`) or engine defaults. `GameManifest::load` resets then applies project settings only after validation; new `GameManifest::unload()` used by editor close/failed-open paths. Project save is explicit (Preferences → Save project settings), never on exit; unsaved project settings trigger the project-switch confirm. Writes are atomic (`.tmp` + rename) and refuse to overwrite unparseable files. Project-owned: GeometryPath, DirShadowDistance, IBLIntensity, Bloom*, AA*, Physics.Enabled/Substeps; everything else per user. Fixed along the way: CullMode always loading None, negative window positions.
- Per user (`user_data_dir()/settings.yaml`): renderer backend, Lite/Full selection, vsync, window placement, editor layout. Per project (in `.honeyproj`): `GeometryPath`, `DirShadowDistance`, anisotropy-style look settings, `Physics:` block, window title/mode, start scene.
- Resolution order: engine default → user → project (project wins for project-owned keys only).
- `Application::set_persist_settings(false)` for the player stays.
- Done when: switching project changes shadow distance/physics settings without touching user hardware settings.

## Step 5 — Migrate content
- Create `Sandbox` (outside the repo, e.g. its own git repo): glTF corpus, HDRIs, textures, audio samples, existing test scenes/prefabs and `assets/scripts` test scripts (`UITest.cs` etc.), `test_game`. Create `FirstGame` empty-template project for the real sample game.
- Rewrite scene/prefab paths through the legacy loader (open + save each once, or a one-shot script); delete `assets/` from the engine repo (and its `.gitignore` noise).
- Done when: `assets/` no longer exists in the engine repo, the editor opens Sandbox and its scenes render, and `FirstGame` opens empty.

## Step 6 — Docs and plan updates
- CLAUDE.md: replace the "asset root / `AssetPaths`" paragraphs and the "Never add `ASSET_ROOT`/`../assets`" rule with the three-tier model; add the dependency-direction invariant and the "editor starts with no project" fitness check; update Key Files.
- `sample_game_plan.md`: phase text referring to `assets/scenes/...` becomes project-relative; Phase 4 export copies `engine_content/` + only referenced project assets + the project's built scripts (4.1/4.2/4.4 get simpler: the project already has a manifest and a user-data dir).

## Open risks
- **Engine default environment:** if the renderer needs an HDRI to look sane, ship one tiny default as engine content rather than depending on the Sandbox's skyboxes.
- **Scenes referencing `assets/...` legacy paths** in both stop-gap and Sandbox; the legacy loader must stay until all are rewritten.
- **Shader overrides:** project `Assets/shaders/` searched before engine content *for custom shaders only*; engine shaders are not overridable by projects (a broken override must not break the editor).
- **Large-file hygiene:** the Sandbox repo should use Git LFS or stay unversioned; not the engine repo's problem anymore.

## Step 0 inventory ✅ (read-only grep of `src/`, `Honey/engine/src`, `Honey/managed`, CMake; 2026-11-17)
Tier key: **E** = engine content (`engine_content/`), **P** = project, **U** = user data, **R** = editor resource (`resources/`, already correct).

| Call site | Tier | Notes |
|---|---|---|
| `assets/shaders/**` (~25 `asset_root()/"shaders"/…` sites: renderer_2d, renderer_3d_{meshlet,deferred,ssao,shadow,bloom,postprocess,lite,debug,vector_icon,pathtracer}, present_pass, vk_cloth_{sim,renderer}, vk_brdf_lut, vk_texture_cube) | E | Mechanical swap to one `engine_content_root()/"shaders"` helper. Pathtracer has 3 local `assets_dir` aliases (lines ~1563/1655/1840). |
| `shader_cache.cpp:87`, `shader_compiler.cpp:251` (include resolution from `shaders/`) | E | Includer must search engine shaders; project custom-shader dir searched first only for project shaders. |
| `scene_viewport_renderer.cpp:343` → `frame_graphs/*.hnfg` | E | All 5 graphs (main/deferred/forward/lite/pathtracing). |
| `imgui_layer.cpp:46-48` → `fonts/JetBrains_Mono/…` | E | Editor/engine UI font. Move that one folder to `engine_content/fonts/`; `Forum` is project (Sandbox/FirstGame). |
| `texture.cpp:28` → `textures/missing.png` | E | Fallback must survive with no project open. |
| `game_ui.cpp:229` (`UI.LoadFont`) | P (+`engine:`) | Relative paths resolve in project; `engine:fonts/...` for engine fonts. |
| `AssetPaths::cache_dir()` users: `renderer.cpp:39` (shader cache), `vk_backend.cpp:1825` (pipeline cache) | U | `assets/cache/` today (untracked). Move to `user_data_dir()/cache`; keyed by engine version/GPU. |
| `AssetPaths::settings_path()` (`engine.cpp:43,121`, `editor_layer.cpp:401`, `renderer_debug_panel.cpp:277`) | U now, split in Step 4 | Hardware keys stay U; look keys (GeometryPath, DirShadowDistance, Physics, …) move to project. |
| `AssetPaths::manifest_path()` (`honey_editor_app.cpp:20`, `honey_runtime_app.cpp:22`, `game_manifest.cpp`), `<asset_root>/../game.yaml` | P | Becomes `.honeyproj`. `resources/defaults/game.yaml` becomes the New Project template. `honey_editor_app.cpp:20` auto-copies a default manifest when missing; must go (editor starts with no project). |
| `runtime_layer.cpp:48`, `game_manifest.cpp:99,106`, `scene_manager.h:13` (`AssetPaths::resolve(scene)`) | P | Start scene / scene list / `Scene::Load`. |
| `scene_serializer.cpp` (14 `to_portable`/`from_portable` sites: sprite, mesh, gltf source, texture, font, icon, audio, script) + `scene.cpp:526,542` (prefab) | P (+`engine:`) | Make project-relative; pass `engine:` through; keep legacy `../assets/…` reader until Step 5. |
| `csharp_script_engine.cpp:73-76` (runtimeconfig, dotnet root, `HoneyEngine.dll`) | E | Already engine-side, not project paths. |
| `csharp_script_engine.cpp:76` `user_scripts_assembly_path()` and `:243` (`asset_root()/"scripts"` as build dir) | P | `Scripts/` + its `bin/` in the project; `-p:HoneyEngineDll=`. |
| `assets/scripts/UserScripts.csproj` HintPath into `Honey/managed/.../HoneyEngine.dll` | P | Replace with `$(HoneyEngineDll)`. |
| `editor_layer.cpp:416,423` (`scripts/libHoneyScripts.so`, native-DLL path) | P | Experimental path; resolve in project `Scripts/`. |
| `scene_hierarchy_panel.cpp` (≈10 sites: 80, 250 `prefabs/`, 696, 754, 788, 814, 848, 870, 948 `scripts/` combo, 1174, 1224) | P | All drag/drop and pickers; route through project root. 250 saves prefabs to `prefabs/<tag>.hnp`. |
| `viewport_panel.cpp:47-52` (drop scene/prefab/gltf) | P | Same. |
| `content_browser_panel.cpp:59,73,80,84,157` | P | Rooted at `Assets/`; icons (60-61) already R. |
| `editor_layer.cpp:40-47` icons via `EditorPaths::resource` | R | Already correct. |
| `CMakeLists.txt:53` `imgui.ini` copied from `resources/defaults/` | U | Editor layout; should live in `user_data_dir()` (per user), default seeded from `resources/defaults/`. |
| `Honey/engine/CMakeLists.txt:425` dev-only source-tree fallback | E | Update to `engine_content/` (+ drop `assets/`). |
| `assets/hdris/skybox*.hdr` | P (Sandbox) | **No code references a specific HDRI** (grep clean); scene files/environment components only. Engine default sky not yet required; re-check in Step 1 by running with no HDRI. |
| `assets/models`, `audio`, `prefabs`, `scenes`, `test_game`, `textures/{bung,seal,flappyBird,flask}` | P (Sandbox) | Pure content; no code refs besides `missing.png`. |
| `assets/imgui.ini` | U | Stale copy; delete. |

**Findings**
1. Engine content is a small, well-bounded set: `shaders/`, `frame_graphs/`, `fonts/JetBrains_Mono/`, `textures/missing.png`. Everything else under `assets/` is Sandbox content.
2. The shader swap is ~25 mechanical sites; shader/pipeline caches are the only things that must move to U.
3. Biggest risks are the editor panels (≈20 `asset_root()` sites, all P) and `honey_editor_app.cpp:20` auto-creating a manifest (violates "editor starts with no project").
4. The engine never hardcodes an HDRI, so no default-sky dependency yet. Recheck in Step 1.
