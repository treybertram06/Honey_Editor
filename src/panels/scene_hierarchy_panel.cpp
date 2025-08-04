#include "scene_hierarchy_panel.h"

#include <imgui.h>


namespace Honey {

    SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& context) {
        set_context(context);
    }

    void SceneHierarchyPanel::set_context(const Ref<Scene>& context) {
        m_context = context;
    }

    void SceneHierarchyPanel::on_imgui_render() {
        ImGui::Begin("Scene Hierarchy");

        if (m_context) {
            auto view = m_context->get_registry().view<TagComponent>();

            for (auto entity : view) {
                Entity current_entity = { entity, m_context.get() };
                draw_entity_node(current_entity);
            }
        }
        ImGui::End();
    }

    void SceneHierarchyPanel::draw_entity_node(Entity entity) {
        auto& tag = entity.get_component<TagComponent>();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_OpenOnDoubleClick |
                ImGuiTreeNodeFlags_SpanAvailWidth |
                    ((m_selected_entity == entity) ? ImGuiTreeNodeFlags_Selected : 0);
        bool opened = ImGui::TreeNodeEx(entity, flags, "%s", tag.tag.c_str());

        if (ImGui::IsItemClicked()) {
            m_selected_entity = entity;
        }

        if (opened) {
            ImGui::TreePop();
        }

    }

}
