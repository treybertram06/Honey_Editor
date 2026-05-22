#pragma once

namespace Honey {
    class EditorLayer;
    class RendererDebugPanel {
    public:
        RendererDebugPanel();
        ~RendererDebugPanel() = default;

        void on_imgui_render(EditorLayer& editor);
        void render_camera_info(EditorLayer& editor);

    private:
        enum class ViewportDisplayMode {
            Color = 0,
            DebugPick = 1
        };

        ViewportDisplayMode m_viewport_display_mode = ViewportDisplayMode::Color;

    };
    
}