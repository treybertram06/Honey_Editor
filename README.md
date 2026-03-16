# Honey Editor

Honey is a modern, high-performance game engine and editor built with C++20. It features a cross-platform architecture supporting OpenGL and Vulkan backends, a high-performance Entity Component System (ECS), and a powerful ImGui-based editor suite.

## Features

### High-Performance Rendering
- **Multi-Backend Architecture**: Seamless support for **OpenGL 4.5** and **Vulkan** (WIP) backends.
- **Batch Renderer 2D**: Highly optimized batch rendering for quads, circles, and lines with support for textures and sub-textures.
- **Advanced Camera Systems**: Orthographic and Perspective cameras with a dedicated Editor Camera featuring smooth interpolation and orbit/pan controls.
- **Shader Management**: Sophisticated shader cache and hot-reloading capabilities.
- **Framebuffer System**: Flexible multi-attachment framebuffers for post-processing and mouse picking.

### Entity Component System (ECS)
- **Driven by EnTT**: Leveraging the industry-standard [EnTT](https://github.com/skypjack/entt) library for maximum cache-friendly performance.
- **Flexible Components**: Built-in support for Transforms, Sprite Renderers, Circle/Line Renderers, Cameras, and Physics.
- **Native Scripting**: Bind C++ classes directly to entities for high-performance logic.
- **Scene Serialization**: Save and load entire worlds using a robust YAML-based serialization system.

### Physics & Audio
- **2D Physics**: Integrated [Box2D](https://github.com/erincatto/box2d) engine with support for rigid bodies, various body types (Static, Dynamic, Kinematic), and multiple collider shapes (Box, Circle).
- **Physics Debug Draw**: Real-time visualization of physics colliders and contact points.
- **Audio System**: Integrated audio support via [SoLoud](https://github.com/jarikomppa/soloud) for spatial and non-spatial sound.

### Editor Suite
- **Visual Scene Hierarchy**: Intuitive tree-based entity management with drag-and-drop parenting.
- **Property Inspector**: Real-time modification of component data with support for math expressions and color pickers.
- **Content Browser**: Filesystem-integrated asset management with thumbnail support.
- **ImGuizmo Integration**: Professional-grade viewport gizmos for intuitive translation, rotation, and scaling of entities.
- **Profiling & Debugging**: Integrated instrumentation for performance bottleneck analysis and log visualization.

## Tech Stack

- **Core**: C++20, CMake 3.16+
- **Graphics**: OpenGL 4.5 / Vulkan, GLFW 3.3, GLM
- **UI**: ImGui, ImGuizmo
- **Systems**: EnTT (ECS), Box2D (Physics), SoLoud (Audio)
- **Utilities**: spdlog (Logging), yaml-cpp (Serialization), stb_image (Textures)

## Requirements

### Software
- **C++20 Compiler**: MSVC 2019+, GCC 11+, or Clang 12+.
- **CMake**: version 3.16 or higher.
- **Vulkan SDK**: Required if building with Vulkan support.

### Platform Dependencies
- **Linux**: Primary supported platform. Requires development headers for X11/Wayland (e.g., `libx11-dev`, `libxcursor-dev`, `libxinerama-dev`, `libxrandr-dev`, `libxi-dev`). `ccache` is highly recommended.
- **Windows**: Requires MSVC 2019+ / Visual Studio 2022, standard Windows SDKs, and the Vulkan SDK if Vulkan support is enabled.
- **macOS**: *Experimental / WIP*. Support is currently lacking and the project likely will not build. Requires Cocoa and Metal frameworks.

## Setup & Build

### 1. Clone the repository
Ensure you clone with submodules if any are used (though most dependencies are vendored directly in `Honey/vendor` and `Honey/engine/vendor`):

```bash
git clone https://github.com/yourusername/Honey_Editor.git
cd Honey_Editor
```

### 2. Configure the Project
Honey uses a standard out-of-source CMake workflow.

> [!WARNING]  
> Windows and macOS support are still limited. Linux is the primary development environment.

#### Linux / Single-config generators
Use `CMAKE_BUILD_TYPE` with Ninja or Makefiles:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

#### Windows / Visual Studio generator
When generating a Visual Studio solution, **do not use `CMAKE_BUILD_TYPE`**. Visual Studio is a **multi-config generator**, so the configuration is selected at build time instead.

```bat
cmake -S . -B build -G "Visual Studio 17 2022"
```

This will generate a Visual Studio solution in the `build` directory.

### 3. Build

#### Linux
```bash
cmake --build build --target Honey_Editor -j$(nproc)
```

#### Windows (Visual Studio generator)
Build the Debug configuration explicitly:

```bat
cmake --build build --config Debug --target Honey_Editor
```

If opening the solution in Visual Studio:

1. Open `build/Honey_Editor.sln`
2. Set configuration to **Debug** or **Release**
3. Build the **Honey_Editor** project

## Running the Editor

Run the editor from the project root so assets are located correctly.

### Linux
```bash
./build/Honey_Editor
```

### Windows (Visual Studio generator)
```bat
.\build\Debug\Honey_Editor.exe
```

### Windows (Ninja)
If configured using Ninja instead of Visual Studio:

```bat
.\build\Honey_Editor.exe
```

## Build Targets
- **Honey_Editor** – Primary desktop editor application.
- **application** – Minimal sandbox environment for testing engine features without editor overhead.

## Notes

### Vulkan on Windows
If building Vulkan-enabled code on Windows, install the Vulkan SDK and ensure the `VULKAN_SDK` environment variable is available in the shell or IDE session used for CMake.

### Generator Differences
- **Ninja / Unix Makefiles**
  - Single-config
  - Uses `CMAKE_BUILD_TYPE`

- **Visual Studio**
  - Multi-config
  - Uses `--config Debug` or `--config Release`

### Recommended Windows Commands

Generate Visual Studio solution:

```bat
cmake -S . -B build -G "Visual Studio 17 2022"
```

Build Debug:

```bat
cmake --build build --config Debug --target Honey_Editor
```

Build Release:

```bat
cmake --build build --config Release --target Honey_Editor
```

## Running the Editor

Once the build is complete, you can launch the editor from the project root to ensure assets are correctly located:

```bash
# Linux/macOS
./build/Honey_Editor

# Windows (if using MSVC generator)
./build/Debug/Honey_Editor.exe
```

### Build Targets
- **Honey_Editor**: The primary desktop application with full editor capabilities.
- **application**: A minimal sandbox environment for testing engine features without the editor overhead.

## Project Structure

- `Honey/`: The core engine codebase.
  - `engine/`: Main framework including Renderer, ECS, and Platform abstraction.
  - `vendor/`: Core engine dependencies (Box2D, yaml-cpp, etc.).
- `src/`: Source code for the Honey Editor application.
- `assets/`: Default shaders, textures, and scene files used by the engine.

## License

This project is currently without an explicit license. Please check back later or contact the maintainer for more information.

---
*This README was brought to you by ChatGPT, because I hate writing READMEs.*
