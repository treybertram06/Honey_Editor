#pragma once
#include "editor_panel.h"

namespace Honey {
    
    class PhysicsDebugPanel : public EditorPanel {
    public:
        PhysicsDebugPanel();
        ~PhysicsDebugPanel() = default;

        const char* name() const override { return "Physics Debug Panel"; }
        void on_imgui_render(EditorLayer& editor) override;

    };
    
}