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

## Step 1 — Split out engine content
- Create `engine_content/` and move shaders, frame graphs, default fonts, and the small default textures/environment the renderer requires. Large sample HDRIs/textures are **not** engine content (they go to Sandbox in step 5); the engine gets a tiny default sky if it needs one.
- `AssetPaths::engine_content_root()` (located like today's asset root: `--engine-content`/env/`<exe>/engine_content`/dev source tree) plus `resolve("engine:...")`. Shader compiler, frame-graph loader, `imgui_layer.cpp` fonts, BRDF LUT, `renderer_*` switch to it.
- Shipped layout: `<exe>/engine_content/`. Shader/pipeline caches move to `user_data_dir()/cache` (engine shaders compile identically for every project).
- The old `assets/` remains as the stop-gap project; everything else in it stays put for now.
- Done when: both executables run with `engine_content/` moved elsewhere (via flag) and no shader/font/frame-graph path references `asset_root()`.

## Step 2 — Project root + project file
- `Project` service (engine): loads/validates `<name>.honeyproj` (YAML), exposes `root()`, `assets_root()`, `scripts_dir()`, `cache_dir()`, name, start scene, scenes, settings overrides, `engine_version`.
- `AssetPaths::asset_root()` becomes `project_assets_root()`; `to_portable`/`from_portable` are project-relative, and `engine:` paths pass through to engine content. Keep accepting legacy `../assets/...` strings so old scenes load until step 5 rewrites them.
- `game.yaml` is absorbed into `.honeyproj` (start scene, scene list, window, vsync); `GameManifest` reads from the project. Player: `--project <path>` or shipped layout `<exe>/game/<name>.honeyproj`.
- `engine_version` mismatch → warning (not error) with a clear message; unknown future fields ignored.
- Done when: `Honey_Runtime --project <path>` runs a project from any folder, and a project folder copied to another machine's engine checkout still loads.

## Step 3 — Editor project workflow
- Startup: `--project <path>` → else last-opened project (user prefs) → else **welcome state** (Open / New / Recent). Editor works with no project (panels empty, play disabled).
- Content browser rooted at `Assets/`; optional read-only "Engine" root for browsing engine content. Open/Save scene, prefab and model dialogs, drag/drop, thumbnails, `to_portable` all go through the project root. Window title shows project name.
- **Scripts:** the project's `Scripts/UserScripts.csproj` references `HoneyEngine.dll` through an MSBuild property (`$(HoneyEngineDll)`), supplied by the editor at build time (`dotnet build -p:HoneyEngineDll=<engine's dll>`) — no HintPath into the engine checkout, so the csproj is portable. The editor builds scripts on project open and on demand, and `dotnet_host` loads `UserScripts.dll` from the project's output. Rebuild is also forced when the engine's managed API changes (we keep hitting `MissingMethodException` otherwise).
- New Project: generates the folder skeleton, `.honeyproj`, `Scripts/UserScripts.csproj`, `.gitignore` (`Cache/`, `bin/`, `obj/`).
- Done when: open project A, then project B, from the editor with no restart issues; a scene saved in one is relative-pathed and loads from a copied folder.

## Step 4 — Settings split
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

## Step 0 inventory (fill in)
| Call site | Tier | Notes |
|---|---|---|
