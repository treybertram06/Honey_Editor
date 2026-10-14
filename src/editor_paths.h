#pragma once

#include <filesystem>

namespace Honey {

    // Editor-only resources (toolbar/content-browser/gizmo icons) that games never ship.
    // Lives in the editor, not the engine, so the player cannot depend on it.
    class EditorPaths {
    public:
        // <exe dir>/resources, falling back to <source root>/resources in dev builds.
        static const std::filesystem::path& resources_root();

        // "icons/toolbar/play_button.png" -> absolute path under resources_root().
        static std::filesystem::path resource(const std::filesystem::path& relative);
    };

}