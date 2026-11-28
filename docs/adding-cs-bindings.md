# Adding a New C# Binding

This guide walks through every file you need to touch to expose a new engine function to C# scripts, using **Instantiate Prefab** as the worked example.

---

## How the bridge works

```
C++ (engine)
  └── NativeFunctionTable  — struct of raw fn pointers (csharp_script_glue.cpp)
        │
        │  passed via Bootstrap() at startup
        ▼
C# (HoneyEngine.dll)
  └── NativeFunctionTable  — mirrored [StructLayout(Sequential)] struct (InternalCalls.cs)
  └── NativeBindings       — typed unsafe wrappers (NativeBindings.cs)
  └── public API           — safe, user-facing classes (e.g. Scene.cs)
```

The two `NativeFunctionTable` structs **must have fields in the exact same order**. That is the one invariant you must not break.

---

## Step-by-step

### 1. Write the C++ glue function — `csharp_script_glue.cpp`

Add a static function that does the actual engine work. It must only use primitive types or raw pointers; no C++ objects cross the boundary.

```cpp
// Returns the UUID of the spawned entity, or 0 on failure.
static uint64_t glue_scene_instantiate_prefab(const char* path) {
    Scene* scene = get_scene();
    HN_CORE_ASSERT(scene, "CSharpScriptGlue: no active scene");
    Entity e = scene->instantiate_prefab(path);   // returns invalid Entity on failure
    return e.is_valid() ? (uint64_t)e.get_uuid() : 0;
}
```

### 2. Add the field to the C++ struct — `csharp_script_glue.cpp`

Open `NativeFunctionTable` and append a new field **at the end** (appending keeps existing field offsets stable):

```cpp
struct NativeFunctionTable {
    // ... existing fields ...
    uint64_t (*Scene_InstantiatePrefab) (const char*);   // ← add here
};
```

### 3. Register the function — `csharp_script_glue.cpp`

In `register_functions`, add the new pointer to the table initializer **in the same position** as the struct field above:

```cpp
static NativeFunctionTable table = {
    // ... existing entries ...
    glue_scene_instantiate_prefab,   // ← add here
};
```

### 4. Mirror the field in the C# struct — `InternalCalls.cs`

Add the matching field **at the end** of the C# `NativeFunctionTable` struct. The type signature must exactly match the C++ function pointer:

```csharp
[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeFunctionTable {
    // ... existing fields ...
    public delegate* unmanaged<byte*, ulong> Scene_InstantiatePrefab;  // ← add here
}
```

Type mapping reference:

| C++ type       | C# unmanaged type |
|----------------|-------------------|
| `uint64_t`     | `ulong`           |
| `int`          | `int`             |
| `float`        | `float`           |
| `uint8_t`      | `byte`            |
| `const char*`  | `byte*`           |
| `float*` (out) | `float*`          |
| `void`         | `void`            |

### 5. Add a typed wrapper — `NativeBindings.cs`

`NativeBindings` is the only place where `unsafe` code lives on the C# side. String → UTF-8 encoding, stackalloc buffers, and pointer arithmetic all go here. Everything above this layer is safe C#.

```csharp
internal static ulong Scene_InstantiatePrefab(string path) {
    // Encode the path as a null-terminated UTF-8 byte buffer.
    int maxBytes = Encoding.UTF8.GetMaxByteCount(path.Length) + 1;
    if (maxBytes <= 512) {
        byte* buf = stackalloc byte[512];
        int written = Encoding.UTF8.GetBytes(path, new Span<byte>(buf, 511));
        buf[written] = 0;
        return InternalCalls.s_table.Scene_InstantiatePrefab(buf);
    } else {
        // Fallback for unusually long paths
        byte[] bytes = Encoding.UTF8.GetBytes(path);
        fixed (byte* p = bytes) {
            byte* buf = (byte*)Marshal.AllocHGlobal(bytes.Length + 1);
            Buffer.MemoryCopy(p, buf, bytes.Length + 1, bytes.Length);
            buf[bytes.Length] = 0;
            ulong result = InternalCalls.s_table.Scene_InstantiatePrefab(buf);
            Marshal.FreeHGlobal((nint)buf);
            return result;
        }
    }
}
```

### 6. Expose a user-facing API

Create or extend a class in `HoneyEngine/src/` that user scripts can call without touching unsafe code. For scene-level operations a static `Scene` class is appropriate:

```csharp
// HoneyEngine/src/Core/Scene.cs
namespace HoneyEngine;

public static class Scene {
    // Spawns a prefab and returns a handle to the new entity.
    // Returns an invalid Entity (ID == 0) if the prefab path was not found.
    public static Entity InstantiatePrefab(string path) {
        ulong id = NativeBindings.Scene_InstantiatePrefab(path);
        return new Entity(id);
    }
}
```

### 7. Rebuild `HoneyEngine.dll`

```bash
cd Honey/managed/HoneyEngine
dotnet build -c Release
```

The output lands in `bin/Release/net10.0/HoneyEngine.dll`, which is the path `CSharpScriptEngine::init()` hardcodes.

---

## Using it in a user script

```csharp
public class Spawner : EntityScript {
    public override void OnCreate() {
        Entity bird = Scene.InstantiatePrefab("prefabs/Bird.hnp");
        if (bird.ID == 0)
            Log.Error("Prefab not found");
    }
}
```

---

## Checklist

- [ ] C++ glue function added to `csharp_script_glue.cpp`
- [ ] Field appended to the C++ `NativeFunctionTable` struct
- [ ] Pointer added to the table initializer in `register_functions` (same position)
- [ ] Field appended to the C# `NativeFunctionTable` struct in `InternalCalls.cs` (same position, matching type)
- [ ] Typed wrapper added to `NativeBindings.cs`
- [ ] Public API added or extended in a user-facing class
- [ ] `HoneyEngine.dll` rebuilt with `dotnet build -c Release`

---

## Common mistakes

**Field order mismatch** — If the C++ and C# structs have fields in different orders, every call after the mismatch will invoke the wrong function. Always append to the end of both structs simultaneously and verify the count matches.

**Missing null terminator** — `const char*` parameters in C++ expect a null-terminated string. Always write `buf[written] = 0` after `GetBytes`.

**Wrong return type** — A `uint64_t` return in C++ maps to `ulong` in C#. An easy mistake is writing `long` (signed) instead.

**Forgetting to rebuild** — The C# struct change won't take effect until `HoneyEngine.dll` is rebuilt. The editor loads the DLL at startup; a stale DLL silently uses the old field layout.