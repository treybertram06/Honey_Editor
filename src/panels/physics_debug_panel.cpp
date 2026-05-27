#include "hnpch.h"
#include "physics_debug_panel.h"
#include "Honey/core/settings.h"
#include "Honey/physics/physics_engine_3d.h"
#include "Honey/renderer/renderer_3d/debug_renderer_3d.h"

#include <imgui.h>


namespace Honey {

    PhysicsDebugPanel::PhysicsDebugPanel() {}

    void PhysicsDebugPanel::on_imgui_render() {
        ImGui::Begin("Physics Debug Panel");

        if (ImGui::CollapsingHeader("Physics Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& physics = Settings::get().physics;
            ImGui::Checkbox("Enable Physics", &physics.enabled);
            ImGui::DragInt("Substeps", &physics.substeps, 1, 1, 20);
        }

        if (ImGui::CollapsingHeader("3D Physics (Jolt)", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& engine  = PhysicsEngine3D::get();
            auto& physics = Settings::get().physics;

            ImGui::Text("Total Bodies:  %u", engine.get_body_count());
            ImGui::Text("Active Bodies: %u", engine.get_active_body_count());

            ImGui::Separator();
            ImGui::Checkbox("Show Debug Shapes", &physics.show_jolt_debug_draw);

            if (physics.show_jolt_debug_draw) {
                auto stats = DebugRenderer3D::get_stats();
                ImGui::Text("Lines drawn:  %u", stats.line_count);
                ImGui::Text("Draw calls:   %u", stats.draw_calls);
                if (stats.dropped_lines > 0)
                    ImGui::TextColored({1, 0.4f, 0, 1}, "Dropped lines: %u (buffer full)", stats.dropped_lines);
                if (ImGui::Button("Reset Stats"))
                    DebugRenderer3D::reset_stats();
            }
        }

        ImGui::End();
    }

}
