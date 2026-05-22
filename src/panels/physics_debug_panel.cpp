#include "hnpch.h"
#include "physics_debug_panel.h"
#include "Honey/core/settings.h"

#include <imgui.h>


namespace Honey {

    PhysicsDebugPanel::PhysicsDebugPanel() {}

    void PhysicsDebugPanel::on_imgui_render() {
        ImGui::Begin("Physics Debug Panel");

        // Physics Settings Section
        if (ImGui::CollapsingHeader("Physics Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& physics = Settings::get().physics;

            ImGui::Checkbox("Enable Physics", &physics.enabled);

            ImGui::DragInt("Substeps", &physics.substeps, 1, 1, 20);

        }

        ImGui::End();
    }

}
