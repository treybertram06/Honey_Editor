Organized by impact (how much difference it makes) × effort (how much work it is). Within each
quadrant, roughly highest-value first.

## Not yet prioritized
- **Have no-longer-used GPU profiler entries be removed from the editor panel.** When the path-tracer is active, its GPU profiling entries (TLAS build, WF: ..., etc.) populate the performance panel and are not removed when the path-tracer is shut down.
- **Make shader compilation errors not cause a crash** Kinda done? I'd like to extend this to using cached shaders/pipelines when shader compilation fails on startup
- **Update components to follow new pattern established by SkyboxComponent** No more plain structs with fully public data

## High impact, low/medium effort — do these first

- **Dynamic shadow map resolution based on camera position.** Fixes a concrete, currently-visible
  artifact (pixelated far shadows when the camera is close to where the shadow lands), not a
  speculative improvement. Scoped to the shadow module.

## High impact, high effort — big investments, worth scheduling deliberately

- **Robust undo/redo system.** No undo/redo infrastructure exists in the codebase today, so this
  is greenfield: a command-pattern layer needs to cover every editor mutation (entity edits,
  component changes, scene-graph moves, etc.), not just one panel. High payoff for editor
  usability, but touches a lot of surface area — plan it as its own multi-step effort rather than
  a quick add.
- **Console interface for automation / agentic interaction.** New subsystem (command parsing +
  binding to editor operations + UI), but high leverage if the goal is scripting the editor or
  driving it from agents — this is the one item here that changes what the editor *can do*, not
  just how it looks or feels.

## Medium impact, medium effort

- **Pull `gpu_types.h` types out of the Vulkan backend.** This is an architecture-health fix, not
  a user-facing one — it's the same class of violation the `VulkanContext`-must-stay-renderer-agnostic
  invariant in `CLAUDE.md` already flags. Worth doing before it causes a real bug, but it's
  mechanical (touches many call sites) rather than risky.
- **Vendor mimalloc / look into arena allocators.** Real perf upside if allocation-heavy paths
  (scene load, ECS churn) are actually a bottleneck — but that hasn't been measured yet. Profile
  first so the effort is spent where it matters.

## Low impact, or high effort relative to payoff — reconsider before picking these up

- **Use `TiledLighting` in the Forward renderer.** Deferred is the primary, actively-developed
  path (`CLAUDE.md`); Forward's actual usage in this project should be checked before investing
  here — low priority unless something specifically needs the forward path.
- **2D renderer major refactor.** "It's currently a mess" is real technical debt, but it's a big,
  open-ended effort (`renderer_2d.cpp`) — worth doing only once it's clear the 2D path still
  matters to the project's direction (the engine's active work is overwhelmingly 3D/Vulkan).
- **C++20 coroutines as an enkiTS supplement.** Exploratory infra change with no concrete pain
  point driving it yet — interesting, but speculative until there's a specific task the current
  task system handles badly.
- **Debug overlays (EditorCamera WP, frame time).** Low effort — `renderer_debug_panel.cpp`
  already exists as a home for this — but also low impact (dev-only tooling, not a blocker on
  anything). Fine as a fill-in task, not worth prioritizing over the items above.

