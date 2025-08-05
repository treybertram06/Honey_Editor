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

        if (entity.has_component<CameraComponent>()) {
            if (ImGui::TreeNodeEx((void*)typeid(CameraComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Camera")) {
                auto& camera_component = entity.get_component<CameraComponent>();
                auto& camera = camera_component.camera;

                bool curr_projection_index = (int)camera_component.projection_type;
                const char* projection_type[] = { "Orthographic", "Perspective" };
                if (ImGui::BeginCombo("Projection", projection_type[curr_projection_index])) {
                    for (int i = 0; i < 2; i++) {
                        bool is_selected = (curr_projection_index == i);
                        if (ImGui::Selectable(projection_type[i], is_selected)) {
                            if (camera_component.projection_type != (CameraComponent::ProjectionType)i) {
                                camera_component.projection_type = (CameraComponent::ProjectionType)i;

                                float current_aspect_ratio = camera_component.camera->get_aspect_ratio();
                                camera_component.update_projection(current_aspect_ratio);
                            }
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }


                if (camera_component.projection_type == CameraComponent::ProjectionType::Orthographic) {
                    bool changed = false;
                    changed |= ImGui::DragFloat("Orthographic Size", &camera_component.orthographic_size, 0.1f, 0.0f, 100.0f);
                    changed |= ImGui::DragFloat("Near Clip", &camera_component.orthographic_near, 0.01f, 0.0f, 100.0f);
                    changed |= ImGui::DragFloat("Far Clip", &camera_component.orthographic_far, 0.01f, 0.0f, 100.0f);

                    if (changed && camera) {
                        if (auto ortho_cam = dynamic_cast<OrthographicCamera*>(camera.get())) {
                            ortho_cam->set_size(camera_component.orthographic_size);
                            ortho_cam->set_near_clip(camera_component.orthographic_near);
                            ortho_cam->set_far_clip(camera_component.orthographic_far);
                        }
                    }
                }

                if (camera_component.projection_type == CameraComponent::ProjectionType::Perspective) {
                    bool changed = false;
                    changed |= ImGui::DragFloat("FOV", &camera_component.perspective_fov, 1.0f, 1.0f, 179.0f);
                    changed |= ImGui::DragFloat("Near Clip", &camera_component.perspective_near, 0.01f, 0.01f, 100.0f);
                    changed |= ImGui::DragFloat("Far Clip", &camera_component.perspective_far, 1.0f, 1.0f, 10000.0f);

                    if (changed && camera) {
                        if (auto persp_cam = dynamic_cast<PerspectiveCamera*>(camera.get())) {
                            persp_cam->set_fov(camera_component.perspective_fov);
                            persp_cam->set_near_clip(camera_component.perspective_near);
                            persp_cam->set_far_clip(camera_component.perspective_far);
                        }
                    }
                }



                ImGui::TreePop();
            }
        }
    }

}
