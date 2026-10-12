#pragma once

#include "editor_panel.h"

namespace Honey {
    class EditorLayer;
    class RendererDebugPanel : public EditorPanel {
    public:
        RendererDebugPanel();
        ~RendererDebugPanel() = default;

        const char* name() const override { return "Renderer Debug Panel"; }
        void on_imgui_render(EditorLayer& editor) override;
        void render_camera_info(EditorLayer& editor);

    private:
        enum class ViewportDisplayMode {
            Color = 0,
            DebugPick = 1
        };

        ViewportDisplayMode m_viewport_display_mode = ViewportDisplayMode::Color;

    };
    
}