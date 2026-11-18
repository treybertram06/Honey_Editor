# Phase 1.3 — Screen-space UI (script API) — Part A ✅ done

Parent: `sample_game_plan.md` → Phase 1.3. Goal: a menu scene shows a title plus Play / Quit buttons, mouse clicks work, and it holds up at different window sizes — in both `Honey_Runtime` and editor Play.

## Invariants (hold across every step)

- **No new Vulkan code, no new descriptors.** Part A is drawn entirely by ImGui (third-party exception to the heap-only rule). If any step seems to need a pipeline, descriptor set or shader of our own, stop and ask.
- **`VulkanContext` is untouched.** Nothing UI-related lives there.
- **The C# API never exposes ImGui.** No `ImGuiWindowFlags`, no ImGui ids/labels with `##`, no ImGui types or enums. Scripts see only our own enums/structs/strings. This is what makes the backend swappable later.
- **Layout is resolution-independent.** Scripts position things with an `Anchor` + offset in *virtual pixels* (reference height 1080), never raw window pixels.
- **Works in editor Play under Lite** (it's ImGui, so it should for free) — but the player requires Full tier as always.
- **Panel begin/end is always balanced natively.** A script exception between `BeginPanel` and `EndPanel` must not be able to crash ImGui.
- Naming/idioms per CLAUDE.md: `snake_case`, `m_`/`s_` prefixes, `HN_CORE_*` in engine, `HN_PROFILE_FUNCTION()`, `hnpch.h` first.

## The design in one paragraph

Two **sibling** UI modules will eventually exist, sharing infrastructure but *not* one API:

1. **Immediate-mode script UI** (this plan): `UI.Button(...)` called from a new `OnGUI()` callback. Best for menus-in-code, prompts ("Press E"), score/HUD text, debug overlays.
2. **Document UI (HTML/CSS-style, Part B)**: `UIDocument.Load("ui/menu.rml")`, look up elements by id, set text, subscribe to clicks. Best for layout-heavy, designer/AI-authored screens.

They cannot be one API: immediate mode is "describe the UI every frame, get the click back as a return value"; a document is "a persistent tree you query and mutate, with event callbacks". Forcing an immediate-mode API onto a DOM (or vice versa) gives a bad version of both. What they *can* share, and what Part A must build as proper seams so Part B is cheap, is: the **UI reference rect + scale**, **input-capture reporting** (`UI.WantsMouse`), and **where in the frame UI is driven from**.

## Where this plugs into the frame (verified in code)

`Application::run` (`core/engine.cpp`): `on_update` for all layers → `m_imgui_layer->begin()` → `on_imgui_render` for all layers → `m_imgui_layer->end()` → swapchain pass (`on_window_render`, e.g. `Renderer::present`) → ImGui draws on top. So ImGui UI is *already* drawn over the presented scene in the swapchain pass. Part A needs no render changes at all.

`RuntimeLayer` currently has no `on_imgui_render`; `EditorLayer` does. Script callbacks are dispatched per entity from `Scene::on_update_scripts` → `CSharpScriptEngine::on_update_entity` — your template for `OnGUI`.

---

## Part A — Immediate-mode script UI — ✅ DONE

Implemented and validated (editor Play, `Honey_Runtime`, Lite). Test scenes: `assets/scenes/ui_test_menu.hns`, `ui_test_game.hns`; scripts: `assets/scripts/UITest.cs`.

**What exists**
- **Native `GameUI`** (`Honey/ui/game_ui.{h,cpp}`): reference rect, scale = height/1080 (clamped 0.5–4), 3x3 anchors, panels as `##hn_ui_<id>` ImGui windows (same id merges into one window), depth counter unwound in `end_frame` so a throwing script can't unbalance ImGui. Widgets: `label` (size, color, font, alignment), `button` (eased hover fade + accent outline), `spacing`, `wants_mouse`.
- **Theme** (`UITheme`): panel/text/accent colors, rounding, padding, spacing, button font size/padding, font. Set from scripts; native defaults are just the starting point.
- **Fonts:** `load_font(path)` (asset-relative or absolute, cached per path, missing file warns once and falls back).
- **Interop:** UI entries at the end of the native function table (`UI_BeginPanel/EndPanel/Label/Button/Spacing/WantsMouse/SetTheme/LoadFont`), UTF-8 marshalling helper in `NativeBindings.cs`.
- **C# API** (`Core/UI.cs`): `UI.Panel(id, Anchor, offset, size, background)` returning a `using`-able `PanelScope`, `UI.Label`, `UI.Button`, `UI.Space`, `UI.WantsMouse`, `UI.Theme`, `UI.LoadFont`, enums `Anchor`, `TextAlign`, structs `UIFont`, class `UITheme`. No ImGui types exposed.
- **`OnGUI` dispatch:** `EntityScript.OnGUI()` virtual; override detected once per instance (`HasOnGUI`), `CallOnGUI` catches and logs once per script type; `Scene::on_gui()` snapshots script entities.
- **Hooks:** `RuntimeLayer::on_imgui_render` (rect = full display); `EditorLayer::on_imgui_render` during Play only (rect = viewport image, skipped when the viewport tab is hidden).
- **Cursor/input:** `ImGuiLayer::begin()` sets `ImGuiConfigFlags_NoMouse` while `Input::is_cursor_locked()` (the GLFW backend still reports a virtual cursor when disabled, so this is required). The editor no longer locks the cursor on viewport click; locking is script-driven (`Cursor.Locked`). Keyboard/gamepad nav for game UI is out of scope.

**Conventions / gotchas**
- A fixed panel `size.x` is the *outer* width (padding included); content wider than it is clipped. Use 0 to auto-fit.
- `UI.WantsMouse` must be checked by gameplay scripts that poll `Input.*`, which ignores UI.
- Editor Play UI windows are floating ImGui windows: tall panels can overflow the viewport. Accepted.
- Changing managed engine API (including optional params) requires rebuilding `UserScripts` (`dotnet build assets/scripts/UserScripts.csproj`), or scripts hit `MissingMethodException`.

**Findings worth keeping:** fonts load via `AssetPaths::asset_root()` so the player has them; ImGui here has Docking+Viewports enabled globally (keep panels clamped inside the rect); input is polled straight from GLFW.

---

## Part B — HTML/CSS-style document UI (future, scoped now so Part A leaves the right seams)

**What's realistic.** Embedding a real browser (CEF/Ultralight) is heavy (hundreds of MB / licensing / offscreen-texture plumbing) — not recommended. The practical fit is **RmlUi**: an embeddable C++ library using **RML** (HTML-like markup) and **RCSS** (CSS-like styling, incl. flexbox in recent versions), with element ids, classes and events. Honest limits: it's *HTML/CSS-flavoured*, not a browser — **no JavaScript**, only a CSS subset. For your goal (AI authors the screens as markup + stylesheet, C# supplies behaviour) it fits well; AI knows HTML/CSS and RML is close, but expect to give it a short "RML dialect" cheat-sheet. *Verify the current RmlUi feature set during the spike below — don't trust this summary blindly.*

**The one big rendering decision (needs your sign-off, hence a gate).** RmlUi draws through a `RenderInterface` you implement (compiled geometry, textures, scissor, optionally layers/filters). Options:
1. **Render RmlUi through ImGui's draw list** (custom vertices + `ImTextureID`s via `ImDrawList::PrimReserve/PrimWriteVtx`, scissor via clip rects). Stays inside the *ImGui third-party exception* → no new descriptors, works under Lite, ~no Vulkan work. Likely limits: no stencil clip masks, filters/shadows/layers need workarounds. Fine for menus/HUD.
2. **RmlUi's own Vulkan backend** — uses classic descriptors → violates the invariant; needs explicit sign-off. Not recommended.
3. **Our own heap-mode UI renderer** — compliant but real renderer work and no Lite support. Only if option 1's limits bite.
**Recommendation: option 1, proven by a spike before committing.**

**Spike (1–2 days):** vendor RmlUi, implement a minimal `RenderInterface` over ImGui's draw list, load one hard-coded RML file, render a styled box with text and an image, click a button, log the event. Gate: if text/images/scissor look right, proceed; if not, revisit.

**Then (outline only — plan properly after the spike):**
1. `UIDocumentSystem` (engine): RmlUi context sized to the `GameUI` reference rect, updated + rendered from the same `on_imgui_render` hook; input forwarded from GLFW state; file interface via `AssetPaths::resolve`.
2. C#: `UIDocument.Load(path)`, `doc.Get("play-button")` → `UIElement` with `Text`, `Visible`, `SetClass`, `OnClick`/`OnChange` events (managed callbacks via function pointer, like script callbacks), `Close()`.
3. Hot reload of `.rml/.rcss` in editor Play (RmlUi supports reloading documents — big iteration win for AI-authored UI).
4. Asset export: include referenced `.rml/.rcss/images/fonts` in the dependency walk (Phase 4).
5. Share `GameUI` rect/scale and `WantsMouse` (OR both systems' capture).

---

## Open decisions / risks
- **Part B is a different model than Part A.** Resist the urge to "unify" them; unify only the shared seams (reference rect/scale, `WantsMouse`, the `on_imgui_render` hook).
