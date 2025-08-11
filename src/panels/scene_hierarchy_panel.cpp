#include "scene_hierarchy_panel.h"

#include <imgui.h>

#include "imgui_internal.h"
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

        // Right click on blank space
        if (ImGui::BeginPopupContextWindow(0, ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
            if (ImGui::MenuItem("Create Empty Entity")) {
                m_context->create_entity("Empty Entity");
            }
            ImGui::EndPopup();
        }


        ImGui::End();

        ImGui::Begin("Properties");

        if (m_selected_entity) {
            draw_components(m_selected_entity);

            if (ImGui::Button("Add Component"))
                ImGui::OpenPopup("Add Component");

            if (ImGui::BeginPopup("Add Component")) {
                if (ImGui::MenuItem("Sprite Renderer")) {
                    m_selected_entity.add_component<SpriteRendererComponent>();
                }
                if (ImGui::MenuItem("Camera")) {
                    m_selected_entity.add_component<CameraComponent>();
                }
                if (ImGui::MenuItem("Native Script")) {
                    m_selected_entity.add_component<NativeScriptComponent>();
                }
                ImGui::EndPopup();
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

        if (ImGui::BeginPopupContextItem(0, ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
            if (ImGui::MenuItem("Delete Entity")) {
                if (m_selected_entity == entity) {
                    m_selected_entity = {};
                }
                m_context->destroy_entity(entity);
            }
            ImGui::EndPopup();
        }

    }

    static void draw_vec3_control(const std::string& label, glm::vec3& values, float reset_value = 0.0f, float column_width = 100.0f) {
        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, column_width);
        ImGui::Text(label.c_str());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

        float line_height = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
        ImVec2 button_size = { line_height + 3.0f, line_height };

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.1f, 0.7f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 0.7f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.1f, 1.0f });
        if (ImGui::Button("X", button_size)) values.x = reset_value; ImGui::SameLine();
        ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f"); ImGui::SameLine();
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(3);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.8f, 0.1f, 0.7f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.9f, 0.2f, 0.7f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.8f, 0.1f, 1.0f });
        if (ImGui::Button("Y", button_size)) values.y = reset_value; ImGui::SameLine();
        ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f"); ImGui::SameLine();
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(3);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.1f, 0.8f, 0.7f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.2f, 0.9f, 0.7f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.1f, 0.8f, 1.0f });
        if (ImGui::Button("Z", button_size)) values.z = reset_value; ImGui::SameLine();
        ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();


        ImGui::Columns(1);
        ImGui::PopID();
    }

    void SceneHierarchyPanel::draw_components(Entity entity) {

        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap;// | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;

        if (entity.has_component<TagComponent>()) {
            auto& tag = entity.get_component<TagComponent>().tag;

            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strcpy(buffer, tag.c_str());
            if (ImGui::InputText("##Tag", buffer, sizeof(buffer))) {
                tag = std::string(buffer);
            }
        }

        if (entity.has_component<TransformComponent>()) {
            bool open = ImGui::TreeNodeEx((void*)typeid(TransformComponent).hash_code(), flags, "Transform");

            if (open) {
                auto& transform_component = entity.get_component<TransformComponent>();
                draw_vec3_control("Translation", transform_component.translation);
                glm::vec3 rotation = glm::degrees(transform_component.rotation);
                draw_vec3_control("Rotation", rotation);
                transform_component.rotation = glm::radians(rotation);
                draw_vec3_control("Scale", transform_component.scale, 1.0f);

                ImGui::TreePop();
            }
        }

        if (entity.has_component<CameraComponent>()) {
            bool open = ImGui::TreeNodeEx((void*)typeid(CameraComponent).hash_code(), flags, "Camera");

            if (open) {
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

        if (entity.has_component<SpriteRendererComponent>()) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
            bool open = ImGui::TreeNodeEx((void*)typeid(SpriteRendererComponent).hash_code(), flags, "Sprite Renderer");

            ImGui::SameLine(ImGui::GetWindowWidth() - 25);
            if (ImGui::Button("+", ImVec2(20, 20)))
                ImGui::OpenPopup("Component Settings");

            ImGui::PopStyleVar();
            bool remove_component = false;
            if (ImGui::BeginPopup("Component Settings")) {
                if (ImGui::MenuItem("Remove Component"))
                    remove_component = true;

                ImGui::EndPopup();
            }

            if (open) {
                auto& sprite_renderer = entity.get_component<SpriteRendererComponent>();
                ImGui::ColorEdit4("Color", glm::value_ptr(sprite_renderer.color));
                ImGui::TreePop();
            }
            if (remove_component) {
                entity.remove_component<SpriteRendererComponent>();
            }
        }

        if (entity.has_component<NativeScriptComponent>()) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
            bool open = ImGui::TreeNodeEx((void*)typeid(NativeScriptComponent).hash_code(), flags, "Native Script");

            ImGui::SameLine(ImGui::GetWindowWidth() - 25);
            if (ImGui::Button("+", ImVec2(20, 20)))
                ImGui::OpenPopup("Component Settings");

            ImGui::PopStyleVar();
            bool remove_component = false;
            if (ImGui::BeginPopup("Component Settings")) {
                if (ImGui::MenuItem("Remove Component"))
                    remove_component = true;

                ImGui::EndPopup();
            }

            if (open) {
                auto& native_script = entity.get_component<NativeScriptComponent>();
                //ImGui::Text("Script: %s", native_script.script->get_class_name());
                ImGui::TreePop();
            }
        }
    }

}
