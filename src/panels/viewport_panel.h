#pragma once

namespace Honey {
    class EditorLayer;

    class ViewportPanel {
    public:
        ViewportPanel();
        ~ViewportPanel() = default;

        void on_imgui_render(EditorLayer& editor);
    };
    
}
