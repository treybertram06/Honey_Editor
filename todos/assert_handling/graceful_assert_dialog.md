# Plan: graceful, continuable asserts (native dialog, cross-thread)

## Context

Today, `HN_CORE_ASSERT`/`HN_ASSERT` (`Honey/engine/src/Honey/core/base.h:46-76`) unconditionally
call `HN_DEBUGBREAK()` on failure. Under a debugger that's a breakpoint you can step past; without
one attached (the common case for a normal run of the editor), the default disposition of an
uncaught `SIGTRAP` is to terminate the process. In practice this means every assert failure is a
hard crash unless you happen to be attached to a debugger at the moment it fires — there is
currently no way to log-and-continue, no way to silence a known-bad assert, and no diagnostic
(callstack) beyond whatever the log line captured.

This surfaced concretely while working through `deferred_renderer/skybox_ibl.md`: a 16K HDRI
dropped onto a `SkyboxComponent` tripped `HN_CORE_ASSERT(pixels, ...)` in
`Honey/engine/src/Honey/loaders/hdr_equirect_loader.cpp:18` (root cause: stb_image's internal
overflow-safe allocation-size check trips at exactly `2^31` bytes — `16384 × 8192 × 4 channels ×
4 bytes/float = 2,147,483,648`, one over `INT_MAX` — so `stbi_loadf` returns `nullptr` regardless
of available RAM) and immediately took down the whole editor. That specific failure is arguably an
input-validation problem that shouldn't assert at all, but it's also a good concrete example of
why *any* assert failure taking down the whole process, with no way to inspect-and-continue, is
too blunt a tool for a full-featured editor.

The goal here is to reproduce the workflow from a past workplace: an assert pops a native dialog
with the message, a callstack, and four options — **Ignore** (just this hit), **Ignore this
assert until restart** (suppress this specific call site), **Ignore all asserts until restart**
(suppress everything), **Abort** (deliberate, clean process termination). All were resumable;
"fatal" there was a severity/reporting distinction, not an unconditional-abort one.

## Inherited invariants — do not violate

- **Must be correct across every thread that can call `HN_CORE_ASSERT`/`HN_ASSERT` today, not just
  the main thread.** Confirmed during research: `load_gltf_mesh_async`
  (`Honey/engine/src/Honey/loaders/gltf_loader.cpp:2149`) dispatches onto `TaskSystem::run_async`
  (background worker threads), and Jolt physics runs its own job threads
  (`Honey/engine/src/Honey/physics/jolt_job_system.cpp`). A design that only works safely from the
  render/main thread (e.g. an ImGui modal driven from the main render loop) is insufficient —
  ImGui is not thread-safe and a worker thread cannot safely inject a modal into the main thread's
  ImGui context without extra marshaling this plan does not build. The chosen approach (shell out
  to a native OS dialog per assert) sidesteps this entirely: each call is a self-contained blocking
  subprocess invocation, independent of the render loop.
- **Never silently hang.** If no dialog backend is available (no `zenity`/`kdialog`, headless/CI,
  no display), fall back to a blocked stderr message + stdin prompt with the same four actions,
  never to an invisible wait.
- **At most one dialog visible at a time**, even under concurrent asserts from multiple threads.
  The global mutex in step 2 below, with a re-check of the ignore-all flag *after* acquiring the
  lock, is what guarantees this — without the re-check, N workers hitting the same assert
  simultaneously pop N redundant dialogs.
- **Ignoring an assert is not the same as fixing the underlying condition.** This system makes
  crash-on-assert avoidable and inspectable; it does not make call sites safe to fall through.
  Existing `HN_CORE_ASSERT` call sites were written assuming failure is fatal, and some dereference
  or otherwise rely on the asserted invariant immediately afterward. Treat "can this call site
  actually tolerate continuing" as a per-site audit, not something this plan retroactively
  guarantees — see step 5.

## Non-goals (explicitly out of scope for this plan)

- **The shader-compiler crash** (`std::runtime_error` thrown out of
  `ShaderCompiler::compile_single_stage`, propagating to an uncaught `std::terminate()`/`SIGABRT`)
  does **not** go through `HN_CORE_ASSERT` and will not be fixed by this work. It needs its own
  `try`/`catch` at the `ShaderCache` call site, probably routing into a sibling
  `report_fatal_error()` that skips the "ignore and keep running" options (a missing SPIR-V module
  isn't something a caller can shrug off the way an out-of-range index might be). Tracked already
  as "Make shader compilation errors not cause a crash" in `todos/short_todos.md`; treat as a
  related but separate follow-up, not part of this plan.
- Windows/macOS debugger-attach detection and native dialog equivalents (`IsDebuggerPresent()` +
  `MessageBox`, `osascript`/`NSAlert`) are not built here — Linux (the primary dev platform, per
  `CLAUDE.md`) only for now. Stub `is_debugger_attached()` to `false` and skip the dialog step
  (falling through to the stderr/stdin path) on other platforms so the build doesn't break, without
  implementing their native paths.
- File/line resolution for the callstack (via `addr2line`) is a nice-to-have, not required for a
  first pass — function names alone from `backtrace_symbols()` are useful without it. Treat as a
  follow-up refinement once the core flow works.

## Design

**Flow:** on assert failure — debugger attached → break in place (today's behavior, unchanged,
since you're already stepping through it and a modal would just steal focus). No debugger →
capture a backtrace, log it, and show the native dialog; act on the chosen button.

**New files:** `Honey/engine/src/Honey/debug/assert_handler.h` / `.cpp` — fits the existing
`debug/` folder's scope (profiling/diagnostic instrumentation) per `CLAUDE.md`'s layout table.
`base.h` is included nearly everywhere, so keep the header side light (no `<execinfo.h>`, no
process-spawning code in the header — just an enum, a function declaration, and the shared atomic).

## Implementation steps

### 1. `assert_handler.h`

```cpp
namespace Honey::Debug {
    enum class AssertAction { IgnoreOnce, IgnoreForever, IgnoreAll, Abort };

    extern std::atomic<bool> g_ignore_all_asserts;

    bool is_debugger_attached();

    AssertAction report_assert(const char* expr, const char* file, int line,
                                const char* function, const std::string& message);
}
```

### 2. `assert_handler.cpp`

- **`is_debugger_attached()`**: read `/proc/self/status`, find the `TracerPid:` line, non-zero
  means attached. No external deps.
- **Backtrace capture** (private helper, not exposed in the header): `backtrace()` +
  `backtrace_symbols()` from `<execinfo.h>`, demangle each frame via `abi::__cxa_demangle`. Skip
  the first couple of frames (they're inside `report_assert`/the macro itself). Return
  `std::vector<std::string>`.
- **The mutex**: one `std::mutex` guarding the whole dialog interaction, held for the duration of
  `report_assert`. Lock it, **then** re-check `g_ignore_all_asserts` before doing any backtrace
  work or spawning a dialog — if another thread set it while this one was waiting on the lock,
  return `IgnoreAll` immediately without showing anything.
- **Always log first**: `HN_CORE_ERROR` with the message + backtrace unconditionally, before the
  debugger-check or dialog, so the log has a permanent record regardless of which button gets
  clicked or whether a display is even available.
- **Spawning the dialog**: `fork()`/`execvp()` (or `posix_spawn`) with an **argv array**, not
  `popen()`/`system()` with a concatenated shell string — the assert message is `fmt::format`-ed
  application text, and building a shell command out of it is an injection hazard even in a dev
  tool. `zenity --question --title=... --text=... --extra-button="Ignore" --extra-button="Ignore
  Always" --extra-button="Ignore All"`, default OK-label doubling as Abort. zenity prints the
  clicked extra-button's label to stdout and exits 1 for extra buttons, exits 0 for the default
  button — capture stdout via a pipe on the child and map it back to the enum.
- **No `zenity`/`kdialog` present**: check once (e.g. `access("/usr/bin/zenity", X_OK)`) and fall
  back to a blocked stderr message + stdin single-keypress prompt (`i`/`g`/`a`/`b`) with the same
  four actions.

### 3. Wire the macros in `base.h`

```cpp
#define HN_CORE_ASSERT(x, ...)                                                          \
    do {                                                                                 \
        static std::atomic<bool> s_ignored_here = false;                                \
        if (!s_ignored_here && !Honey::Debug::g_ignore_all_asserts && !(x)) {            \
            if (Honey::Debug::is_debugger_attached()) {                                  \
                HN_CORE_ERROR("Assertion Failed: {0}", ...);                             \
                HN_DEBUGBREAK();                                                         \
            } else {                                                                     \
                auto action = Honey::Debug::report_assert(#x, __FILE__, __LINE__,        \
                    CURRENT_FUNCTION, fmt::format(__VA_ARGS__));                         \
                if (action == Honey::Debug::AssertAction::IgnoreForever) s_ignored_here = true; \
                if (action == Honey::Debug::AssertAction::IgnoreAll)                     \
                    Honey::Debug::g_ignore_all_asserts = true;                           \
                if (action == Honey::Debug::AssertAction::Abort) std::abort();           \
            }                                                                            \
        }                                                                                \
    } while (0)
```

Mirror the same shape for `HN_ASSERT`. Note `Abort` calls `std::abort()` directly rather than
`HN_DEBUGBREAK()` — the point of this feature is a deliberate, guaranteed termination when the
user picks it, not a `SIGTRAP` that depends on something catching it (that inconsistency is the
bug this whole plan exists to fix).

Each `HN_CORE_ASSERT(...)` call site expands its own distinct `static` local, so "ignore this
call site until restart" falls out for free — no registry or keying by file/line needed.

### 4. Per-call-site fallthrough audit (ongoing, not blocking)

Not every existing `HN_CORE_ASSERT` call site needs to be audited before this ships — the dialog
system is strictly additive (today's behavior was "always fatal"; this makes fatal *optional*).
But flag it in review/PRs going forward: a call site that immediately dereferences the thing it
just asserted on (the HDR loader is the concrete example — `stbi_loadf` failing and the code
still going on to read `width`/`height` as if `pixels` were valid) will misbehave *differently*
after "Ignore" than it did before (undefined behavior instead of a clean crash), not necessarily
*better*. Genuinely recoverable failures (bad/oversized user-supplied assets, in particular) are
arguably better served by being converted to a real early-return + logged error + editor toast —
matching the pattern the `.glb`/`.gltf` drag-drop path in `scene_hierarchy_panel.cpp` already
uses — rather than routed through assert at all, regardless of how graceful the assert UI gets.

## Verification checklist

- Run the built binary directly (not through CLion's debugger) and trigger an assert — dialog
  appears; all four buttons behave as expected (Ignore re-prompts on the next hit of the same
  line; Ignore Always/Ignore All suppress within this run; Abort cleanly terminates).
- Trigger the same assert twice in a row after choosing plain "Ignore" — it prompts again the
  second time.
- Force an assert failure inside a `TaskSystem::run_async` job (background thread) — dialog still
  appears correctly, and the main thread's render state is unaffected.
- Trigger two different asserts from two threads at once — only one dialog shows at a time; the
  second either shows next or is suppressed by "Ignore All," never both dialogs simultaneously.
- Attach CLion's debugger and trigger an assert — breaks in place instead of spawning a dialog.
- Temporarily rename/hide `zenity` and trigger an assert — the stdin fallback kicks in instead of
  hanging.