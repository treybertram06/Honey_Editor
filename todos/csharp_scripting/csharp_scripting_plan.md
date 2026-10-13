# C# Scripting — Implementation Plan

Replaces Lua as the primary scripting language using the .NET CoreCLR hosting API
(`hostfxr` / `nethost`). Lua scripting continues to live untouched throughout; the two
systems run side-by-side until C# is feature-complete and Lua can be retired.

Status markers:
- ✅ Done
- 🔲 Not started

---

## Architecture Overview

The canonical way to embed .NET in a native C++ host — used by Unity, Godot 4, and
the reference Microsoft samples — is the three-layer model below. Never try to link
against CoreCLR directly; always go through `hostfxr`.

```
┌─────────────────────────────────────────────────────┐
│  C++ Engine (libHoney / Editor)                     │
│                                                     │
│  CSharpScriptEngine   ──calls──►  DotNetHost        │
│       │                              │              │
│       │                    dlopen(libhostfxr.so)    │
│       │                    hostfxr_initialize_*     │
│       │                    get_runtime_delegate     │
│       │                              │              │
│  CSharpScriptGlue  ◄─internal calls─┤              │
│  (registers C++ fn ptrs)            │              │
└─────────────────────────────────────┼──────────────┘
                                      │ load_assembly_and_get_function_pointer
                          ┌───────────▼───────────────┐
                          │  HoneyEngine.dll  (C#)    │
                          │  (managed bridge)         │
                          │                           │
                          │  ScriptRegistry           │
                          │  EntityScript (base class)│
                          │  Entity / Transform / etc │
                          │  InternalCalls (glue)     │
                          └───────────────────────────┘
                                      │ user inherits
                          ┌───────────▼───────────────┐
                          │  PlayerController.cs      │
                          │  EnemyAI.cs  etc.         │
                          │  (user script assembly)   │
                          └───────────────────────────┘
```

### Key design decisions

- **One `hostfxr` init per process** — CoreCLR is a singleton. Init once in
  `CSharpScriptEngine::init()`, never again.
- **`AssemblyLoadContext` (ALC) for hot reload** — user scripts live inside a
  collectible ALC. Hot-reload = dispose old ALC, create new one, reload the DLL.
  `HoneyEngine.dll` lives in the *default* ALC and is never unloaded.
- **Internal calls, not P/Invoke** — `CSharpScriptGlue` registers a table of
  C function pointers. The managed bridge retrieves them through a single bootstrap
  call at startup. This avoids DllImport path headaches and is trivially extensible.
- **`[UnmanagedCallersOnly]` bootstrap** — one static C# method with this attribute
  acts as the only hard-wired entry point. Everything else is discovered through the
  internal call table.
- **`ScriptComponent` gains a `language` field** — existing Lua scenes deserialise
  unchanged (default is Lua). New C# scripts set `language: CSharp` in the YAML.
  The dispatcher in `editor_layer.cpp` / `scene.cpp` routes to the right engine.

---

## File Layout (target state)

```
Honey/engine/
  src/Honey/scripting/
    script_engine.h / .cpp          ← unchanged (Lua)
    script_glue.h / .cpp            ← unchanged (Lua)
    csharp/
      csharp_script_engine.h        ← Phase 4
      csharp_script_engine.cpp
      csharp_script_glue.h          ← Phase 3
      csharp_script_glue.cpp
      dotnet_host.h                 ← Phase 1
      dotnet_host.cpp
  vendor/dotnet/                    ← already vendored
    include/  hostfxr.h  coreclr_delegates.h  nethost.h
    native/   libnethost.a

assets/
  scripts/
    *.lua                           ← existing Lua scripts, untouched
  scripts-cs/
    *.cs                            ← user C# source (compiled externally)

managed/
  HoneyEngine/                      ← Phase 2 — managed bridge C# project
    HoneyEngine.csproj
    src/
      Core/
        EntityScript.cs             ← base class users inherit
        ScriptRegistry.cs           ← maps class name → type for reflection
        Log.cs
      Interop/
        InternalCalls.cs            ← receives the C++ fn-ptr table
        NativeBindings.cs           ← typed wrappers around fn ptrs
      Components/
        Entity.cs
        Transform.cs
        RigidBody.cs
      Math/
        Vector2.cs  Vector3.cs  Vector4.cs  Quaternion.cs
  HoneyEngine.ScriptCore/           ← thin re-export shim (optional, avoids
    HoneyEngine.ScriptCore.csproj     shipping HoneyEngine.dll to end users)
```

---

## Phase 1 — DotNetHost (C++ runtime bootstrapper) 🔲

**Goal:** wrap `hostfxr` in an RAII C++ class with a clean API.

`DotNetHost` responsibilities:
- Call `get_hostfxr_path` from `nethost.h` to locate the installed runtime.
- `dlopen` (Linux) / `LoadLibrary` (Windows) the found `libhostfxr.so`.
- Call `hostfxr_initialize_for_runtime_config` with a `.runtimeconfig.json`
  that lives next to `HoneyEngine.dll`.
- Call `hostfxr_get_runtime_delegate(hdt_load_assembly_and_get_function_pointer)`
  to obtain the primary delegate used in all later phases.
- Expose `load_managed_fn(assembly, type, method, delegate_type) -> void*`.

```cpp
// dotnet_host.h
class DotNetHost {
public:
    bool init(const std::filesystem::path& runtime_config_path);
    void shutdown();

    // Returns a callable C fn ptr for the named managed method.
    void* load_managed_function(
        std::string_view assembly_path,
        std::string_view type_name,
        std::string_view method_name,
        std::string_view delegate_type_name = "");

private:
    void* m_hostfxr_lib = nullptr;
    void* m_load_assembly_fn = nullptr;    // load_assembly_and_get_function_pointer_fn
    hostfxr_handle m_host_ctx = nullptr;
};
```

`.runtimeconfig.json` template (emit once, check into repo alongside HoneyEngine.dll):
```json
{
  "runtimeOptions": {
    "tfm": "net9.0",
    "framework": {
      "name": "Microsoft.NETCore.App",
      "version": "9.0.0"
    }
  }
}
```

---

## Phase 2 — HoneyEngine.dll (managed bridge C# library) 🔲

**Goal:** give user scripts a clean, versioned C# API that mirrors the engine's ECS.

### 2.1 EntityScript base class

```csharp
// EntityScript.cs
public abstract class EntityScript {
    internal ulong EntityID { get; set; }

    public Entity Entity => new Entity(EntityID);

    public virtual void OnCreate()  {}
    public virtual void OnUpdate(float dt) {}
    public virtual void OnDestroy() {}
    public virtual void OnCollisionBegin(Entity other) {}
    public virtual void OnCollisionEnd(Entity other)   {}
}
```

### 2.2 Internal calls bootstrap

One `[UnmanagedCallersOnly]` entry point receives a pointer to a C-struct of function
pointers. The struct is declared in both C++ and C#; layout must match exactly.

```csharp
// InternalCalls.cs
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeFunctionTable {
    public delegate* unmanaged<ulong, float*, void>  Entity_GetTranslation;
    public delegate* unmanaged<ulong, float*, void>  Entity_SetTranslation;
    public delegate* unmanaged<ulong, float*, void>  Entity_GetRotation;
    public delegate* unmanaged<ulong, float*, void>  Entity_SetRotation;
    public delegate* unmanaged<ulong, float*, void>  Entity_GetScale;
    public delegate* unmanaged<ulong, float*, void>  Entity_SetScale;
    public delegate* unmanaged<ulong, float, float, float, void>  RigidBody_ApplyForce;
    public delegate* unmanaged<byte*, void>          Log_Info;
    public delegate* unmanaged<byte*, void>          Log_Warn;
    public delegate* unmanaged<byte*, void>          Log_Error;
    // extend freely — new fields at the end for ABI compatibility
}

public static unsafe class InternalCalls {
    private static NativeFunctionTable s_table;

    [UnmanagedCallersOnly]
    public static void Bootstrap(NativeFunctionTable* table) {
        s_table = *table;
    }

    internal static void Entity_GetTranslation(ulong id, out Vector3 v) { ... }
    // typed wrappers that call through s_table
}
```

The C++ side at startup:
```cpp
// csharp_script_glue.cpp
static NativeFunctionTable s_table = {
    .Entity_GetTranslation = &glue_entity_get_translation,
    .Entity_SetTranslation = &glue_entity_set_translation,
    // ...
};
// Then call the Bootstrap function pointer obtained from DotNetHost.
```

### 2.3 Entity / Transform / component wrappers

```csharp
public readonly struct Entity {
    internal readonly ulong ID;
    public ref Transform Transform => ...;
    public bool HasComponent<T>() where T : Component => ...;
    public T GetComponent<T>() where T : Component => ...;
}
```

### 2.4 Math types

Interop-safe structs with `[StructLayout(LayoutKind.Sequential)]` that marshal
transparently to `glm::vec3` / `glm::quat`. Implement operators (+, *, etc.) and
`static readonly` constants (Vector3.Zero, Vector3.Up, etc.).

### 2.5 Input

```csharp
public static class Input {
    public static bool IsKeyDown(KeyCode key)       => InternalCalls.Input_IsKeyDown((int)key);
    public static bool IsMouseButtonDown(int button) => ...;
    public static Vector2 GetMousePosition()         => ...;
}
```

### 2.6 ScriptRegistry

```csharp
// ScriptRegistry.cs
public static class ScriptRegistry {
    private static readonly Dictionary<string, Type> s_types = new();

    [UnmanagedCallersOnly]
    public static void RegisterAssembly(char* assemblyPathPtr) {
        // Load assembly, find all EntityScript subclasses, add to s_types.
    }

    [UnmanagedCallersOnly]
    public static IntPtr CreateInstance(char* classNamePtr) {
        // Activator.CreateInstance, wrap in GCHandle, return handle as IntPtr.
    }

    [UnmanagedCallersOnly]
    public static void DestroyInstance(IntPtr handle) {
        GCHandle.FromIntPtr(handle).Free();
    }
    // ...
}
```

The C++ side stores `IntPtr` (= `void*`) as the script instance handle. It never
touches the managed object directly; it only passes the handle back on calls.

---

## Phase 3 — CSharpScriptGlue (C++ internal call implementations) 🔲

**Goal:** implement every entry in `NativeFunctionTable` on the C++ side.

Structure mirrors `script_glue.cpp`. Each function is a plain `extern "C"` (or a
matching calling convention) static function that reads/writes engine state.

```cpp
// csharp_script_glue.cpp

static void glue_entity_get_translation(uint64_t entity_id, float* out_xyz) {
    Entity e = CSharpScriptEngine::get_scene_context()->get_entity_by_uuid(entity_id);
    auto& tc = e.get_component<TransformComponent>();
    out_xyz[0] = tc.translation.x;
    out_xyz[1] = tc.translation.y;
    out_xyz[2] = tc.translation.z;
}

static void glue_log_info(const char* msg) {
    HN_INFO("[C#] {}", msg);
}
// ...

void CSharpScriptGlue::register_functions(DotNetHost& host) {
    // Build the table, get the Bootstrap fn ptr, call it.
    static NativeFunctionTable table = { ... };
    auto bootstrap = (BootstrapFn)host.load_managed_function(
        HONEY_ENGINE_DLL_PATH,
        "HoneyEngine.InternalCalls, HoneyEngine",
        "Bootstrap",
        UNMANAGEDCALLERSONLY_METHOD);
    bootstrap(&table);
}
```

Extend the table freely — add new fields at the end. C# reads `sizeof(NativeFunctionTable)`
and only uses fields it knows about, so older managed DLLs stay compatible with a newer host.

---

## Phase 4 — CSharpScriptEngine (lifecycle manager) 🔲

**Goal:** a class with exactly the same surface as `ScriptEngine` (Lua), routing to CoreCLR.

```cpp
// csharp_script_engine.h
class CSharpScriptEngine {
public:
    static void init();
    static void shutdown();

    static void on_runtime_start(Scene* scene);
    static void on_runtime_stop();

    static bool entity_class_exists(const std::string& class_name);
    static void on_create_entity(Entity entity);
    static void on_update_entity(Entity entity, Timestep ts);
    static void on_destroy_entity(Entity entity);
    static void on_collision_begin(Entity a, Entity b);
    static void on_collision_end(Entity a, Entity b);

    static Scene* get_scene_context();

    // Hot reload: unload the user script ALC and reload from disk.
    static void reload_scripts();

    template<typename T>
    static std::optional<T> get_property(Entity entity, const std::string& name);

private:
    struct Data;
    static std::unique_ptr<Data> s_data;
};
```

### Internal Data

```cpp
struct CSharpScriptEngine::Data {
    DotNetHost host;
    Scene* scene_context = nullptr;

    // Fn ptrs obtained at init from HoneyEngine.dll
    using CreateInstanceFn  = void* (*)(const char*);
    using DestroyInstanceFn = void  (*)(void*);
    using CallOnCreateFn    = void  (*)(void*, uint64_t);
    using CallOnUpdateFn    = void  (*)(void*, uint64_t, float);
    using CallOnDestroyFn   = void  (*)(void*, uint64_t);
    using CallCollisionFn   = void  (*)(void*, uint64_t, uint64_t);
    using GetPropertyFn     = bool  (*)(void*, const char*, PropertyType, void*);

    CreateInstanceFn  create_instance  = nullptr;
    DestroyInstanceFn destroy_instance = nullptr;
    CallOnCreateFn    call_on_create   = nullptr;
    CallOnUpdateFn    call_on_update   = nullptr;
    CallOnDestroyFn   call_on_destroy  = nullptr;
    CallCollisionFn   call_collision_begin = nullptr;
    CallCollisionFn   call_collision_end   = nullptr;
    GetPropertyFn     get_property     = nullptr;

    // Per-entity script instances (managed GCHandle stored as void*)
    std::unordered_map<UUID, void*> entity_instances;
};
```

All fn ptrs are obtained once at `init()` via `DotNetHost::load_managed_function`.

### Hot reload flow

```
reload_scripts():
  1. For each live instance: call_on_destroy + destroy_instance
  2. entity_instances.clear()
  3. Call managed UnloadScriptAssembly (disposes the ALC)
  4. Call managed LoadScriptAssembly(new_path) (creates fresh ALC)
  5. Re-create instances for all running entities (if in play mode)
```

---

## Phase 5 — ScriptComponent extension + serialization 🔲

**Goal:** single `ScriptComponent` dispatches to Lua or C# transparently.

### components.h change

```cpp
enum class ScriptLanguage : uint8_t { Lua = 0, CSharp = 1 };

struct ScriptComponent {
    std::string    script_name;
    ScriptLanguage language = ScriptLanguage::Lua;     // NEW
    std::unordered_map<std::string, std::variant<float, bool, std::string>> property_overrides;
    // ...
};
```

Default `Lua` means all existing scenes deserialise correctly.

### SceneSerializer change

```yaml
# existing Lua (unchanged, no language field → default Lua)
ScriptComponent:
  ClassName: PlayerController

# new C# script
ScriptComponent:
  ClassName: PlayerController
  Language: CSharp
```

Read: if `Language` key is absent, default to `Lua`.

### Dispatch sites (scene.cpp / editor_layer.cpp)

Anywhere the engine currently calls `ScriptEngine::on_create_entity(entity)` etc.,
add a branch:

```cpp
if (sc.language == ScriptLanguage::CSharp)
    CSharpScriptEngine::on_create_entity(entity);
else
    ScriptEngine::on_create_entity(entity);
```

The two engines have identical method signatures intentionally so this is mechanical.

---

## Phase 6 — Editor integration 🔲

**Goal:** the editor's inspector and script picker work for C# scripts just like Lua.

### Script picker

The existing `ScriptComponent` UI widget browses `assets/scripts/`. Extend it to also
show `assets/scripts-cs/*.cs` (or the compiled `.dll` — depends on workflow decision
in Phase 7). Tag C# scripts with a small `[C#]` label next to the name.

### Properties panel

No change required. The properties panel already reads from `ScriptComponent::property_overrides`
and calls `ScriptEngine::get_default_properties`. Replicate the same call on
`CSharpScriptEngine` — it uses reflection on the managed side to enumerate `public` fields.

```csharp
// ScriptRegistry.cs — called from C++
[UnmanagedCallersOnly]
public static IntPtr GetDefaultProperties(void* instanceHandle) {
    // Use reflection to find all public fields on the EntityScript instance.
    // Pack into a format the C++ side can read (e.g. a serialised JSON blob or
    // a fixed PropertyDescriptor[] array passed through a shared buffer).
}
```

Recommended: pack into a flat array of `PropertyDescriptor` structs (name + type tag +
value bytes) allocated in native memory via `NativeMemory.Alloc`; C++ frees after read.

### Add-Component shortcut

When the user drags a `.cs` file from the content browser onto an entity, auto-set
`language = CSharp`. The existing `.lua` drag path stays unchanged.

---

## Phase 7 — Hot reload via AssemblyLoadContext 🔲

**Goal:** user scripts can be recompiled and reloaded without restarting the editor.

### Build workflow

User scripts are a standalone `.csproj` that references `HoneyEngine.dll`:

```xml
<!-- UserScripts.csproj -->
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net9.0</TargetFramework>
    <AssemblyName>UserScripts</AssemblyName>
  </PropertyGroup>
  <ItemGroup>
    <Reference Include="HoneyEngine">
      <HintPath>$(HoneyEngineDllPath)</HintPath>
    </Reference>
  </ItemGroup>
</Project>
```

The editor can shell out `dotnet build UserScripts.csproj` and watch for file changes.
Alternatively, provide a "Rebuild Scripts" button in the editor toolbar.

### ALC hot reload

```csharp
// ScriptRegistry.cs
private static WeakReference<AssemblyLoadContext>? s_scriptAlc;

[UnmanagedCallersOnly]
public static void LoadScriptAssembly(char* pathPtr) {
    var alc = new AssemblyLoadContext("UserScripts", isCollectible: true);
    s_scriptAlc = new WeakReference<AssemblyLoadContext>(alc);
    var asm = alc.LoadFromAssemblyPath(Marshal.PtrToStringAnsi((IntPtr)pathPtr)!);
    // Scan for EntityScript subclasses, register in s_types.
}

[UnmanagedCallersOnly]
public static void UnloadScriptAssembly() {
    if (s_scriptAlc?.TryGetTarget(out var alc) == true)
        alc.Unload();
}
```

> Note: after `Unload()` the ALC is not immediately collected — it is collected when
> the GC runs and all references are gone. For hot reload this is fine; call `GC.Collect`
> a few times if you want to confirm the old assembly is gone (useful in tests).

---

## Extensibility Notes

The architecture above is deliberately open for future additions:

- **More component bindings**: add a field to `NativeFunctionTable`, implement the C++ glue,
  add a typed C# wrapper. No structural change required.
- **Coroutines / async**: `EntityScript` can expose `IEnumerator StartCoroutine(...)` backed
  by a managed scheduler that calls `Resume()` each frame from `on_update_entity`.
- **Editor scripting**: the same `DotNetHost` can load a separate editor-scripts ALC so users
  can write editor tools in C#.
- **Debugging**: since we host CoreCLR the managed debugger protocol is available. The editor
  can advertise a debug port and VS/Rider can attach transparently.
- **Source generators**: add a Roslyn source generator to `HoneyEngine` to auto-generate
  `[UnmanagedCallersOnly]` wrappers and `NativeFunctionTable` entries from a single attribute,
  eliminating manual sync between C++ and C# table definitions.

---

## Implementation Order

| Phase | Description                              | Depends on |
|-------|------------------------------------------|------------|
| 1     | DotNetHost C++ wrapper                   | —          |
| 2     | HoneyEngine.dll managed bridge           | —          |
| 3     | CSharpScriptGlue internal calls          | 1, 2       |
| 4     | CSharpScriptEngine lifecycle manager     | 1, 2, 3    |
| 5     | ScriptComponent + serialization changes  | 4          |
| 6     | Editor inspector / script picker         | 4, 5       |
| 7     | Hot reload via ALC                       | 4          |

Phases 1 and 2 are entirely independent and can be done in parallel. Spike Phase 1
first since it has the highest unknowns (finding the right runtime on disk, getting
the bootstrap call to fire).