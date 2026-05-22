#pragma once

namespace Honey {
    
    class PhysicsDebugPanel {
    public:
        PhysicsDebugPanel();
        ~PhysicsDebugPanel() = default;

        void on_imgui_render();

    };
    
}