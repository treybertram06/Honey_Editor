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
- **macOS**: *Experimental / WIP*. Support is currently lacking and the project likely will not build. Requires Cocoa and Metal frameworks.
- **Windows**: *Experimental / WIP*. Support is currently lacking and the project likely will not build. Requires MSVC 2019+ and standard Windows SDKs.

## Setup & Build

### 1. Clone the repository
Ensure you clone with submodules if any are used (though most are vendored directly in `Honey/vendor` and `Honey/engine/vendor`):
```bash
git clone https://github.com/yourusername/Honey_Editor.git
cd Honey_Editor
```

### 2. Configure the Project
Honey uses a standard CMake workflow. We recommend using a separate build directory.

> [!WARNING]  
> Windows and macOS support is currently very limited and the project may not build successfully on these platforms at this time. Linux is the primary development environment.

```bash
# Create build directory and configure
cmake -B build -DCMAKE_BUILD_TYPE=Debug
```
*Tip: On Linux, ensure you have the required X11/Wayland development packages installed before this step.*

### 3. Build
You can build the entire project or target the editor specifically:
```bash
# Build the Editor (recommended)
cmake --build build --target Honey_Editor -j$(nproc)
```
The `-j$(nproc)` flag tells CMake to use all available CPU cores for a faster build.

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