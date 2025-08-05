#include "scene_hierarchy_panel.h"

#include <imgui.h>

#include "glm/gtc/type_ptr.inl"


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

        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
                m_selected_entity = {};
        }

        ImGui::End();

        ImGui::Begin("Properties");

        if (m_selected_entity) {
            draw_components(m_selected_entity);
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

    void SceneHierarchyPanel::draw_components(Entity entity) {
        if (entity.has_component<TagComponent>()) {
            auto& tag = entity.get_component<TagComponent>().tag;

            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strcpy(buffer, tag.c_str());
            if (ImGui::InputText("Tag", buffer, sizeof(buffer))) {
                tag = std::string(buffer);
            }
        }

        if (entity.has_component<TransformComponent>()) {

            if (ImGui::TreeNodeEx((void*)typeid(TransformComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Transform")) {
                auto& transform = entity.get_component<TransformComponent>().transform;
                ImGui::DragFloat3("Position", glm::value_ptr((transform[3])), 0.01f);

                ImGui::TreePop();
            }


        }
    }

}
