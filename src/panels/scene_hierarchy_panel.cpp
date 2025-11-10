#include "scene_hierarchy_panel.h"

#include <filesystem>
#include <imgui.h>

#include "imgui_internal.h"
#include "glm/gtc/type_ptr.inl"
#include "Honey/renderer/texture.h"
#include "Honey/scene/script_registry.h"


namespace Honey {

    extern const std::filesystem::path g_assets_dir;

    SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& context) {
        set_context(context);
    }

    void SceneHierarchyPanel::set_context(const Ref<Scene>& context) {
        m_selected_entity = {};
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
        }

        ImGui::End();
    }

    void SceneHierarchyPanel::draw_entity_node(Entity entity) {
        auto& tag = entity.get_component<TagComponent>();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth |
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
        ImGuiIO& io = ImGui::GetIO();
        auto extra_bold_font = io.Fonts->Fonts[1];

        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, column_width);
        ImGui::Text("%s", label.c_str());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

        float line_height = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
        ImVec2 button_size = { line_height + 3.0f, line_height };

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.1f, 0.7f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 0.7f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.1f, 1.0f });
        ImGui::PushFont(extra_bold_font);
        if (ImGui::Button("X", button_size)) values.x = reset_value; ImGui::SameLine();
        ImGui::PopFont();
        ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f"); ImGui::SameLine();
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(3);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.8f, 0.1f, 0.7f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.9f, 0.2f, 0.7f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.8f, 0.1f, 1.0f });
        ImGui::PushFont(extra_bold_font);
        if (ImGui::Button("Y", button_size)) values.y = reset_value; ImGui::SameLine();
        ImGui::PopFont();
        ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f"); ImGui::SameLine();
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(3);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.1f, 0.8f, 0.7f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.2f, 0.9f, 0.7f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.1f, 0.8f, 1.0f });
        ImGui::PushFont(extra_bold_font);
        if (ImGui::Button("Z", button_size)) values.z = reset_value; ImGui::SameLine();
        ImGui::PopFont();
        ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();


        ImGui::Columns(1);
        ImGui::PopID();
    }

    template<typename T, typename UIFunction>
    static void draw_component(const std::string& label, Entity entity, UIFunction ui_function) {

        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_FramePadding;

        if (entity.has_component<T>()) {

            auto& component = entity.get_component<T>();
            ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
            float line_height = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
            ImGui::Separator();
            bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), flags, "%s", label.c_str());
            ImGui::PopStyleVar();

            ImGui::SameLine(contentRegionAvailable.x - line_height * 0.5f);
            if (ImGui::Button("+", ImVec2(line_height, line_height)))
                ImGui::OpenPopup("Component Settings");

            bool remove_component = false;
            if (ImGui::BeginPopup("Component Settings")) {
                if (ImGui::MenuItem("Remove Component"))
                    remove_component = true;

                ImGui::EndPopup();
            }

            if (open) {
                ui_function(component);
                ImGui::TreePop();
            }
            if (remove_component)
                entity.remove_component<T>();
        }
    }
    void SceneHierarchyPanel::draw_components(Entity entity) {

        if (entity.has_component<TagComponent>()) {
            auto& tag = entity.get_component<TagComponent>().tag;

            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strcpy(buffer, tag.c_str());
            if (ImGui::InputText("##Tag", buffer, sizeof(buffer))) {
                tag = std::string(buffer);
            }
        }

        ImGui::SameLine();
        ImGui::PushItemWidth(-1);
        if (ImGui::Button("Add Component"))
            ImGui::OpenPopup("Add Component");

        if (ImGui::BeginPopup("Add Component")) {
            if (ImGui::MenuItem("Transform Component")) {
                entity.add_component<TransformComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Sprite Renderer")) {
                entity.add_component<SpriteRendererComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Camera Component")) {
                entity.add_component<CameraComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Native Script Component")) {
                entity.add_component<NativeScriptComponent>();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::PopItemWidth();


        draw_component<TransformComponent>("Transform", entity, [](auto& component) {
            draw_vec3_control("Translation", component.translation);
                glm::vec3 rotation = glm::degrees(component.rotation);
                draw_vec3_control("Rotation", rotation);
                component.rotation = glm::radians(rotation);
                draw_vec3_control("Scale", component.scale, 1.0f);
        });

        draw_component<CameraComponent>("Camera", entity, [](auto& component) {
            auto& camera = component.camera;

                bool curr_projection_index = (int)component.projection_type;
                const char* projection_type[] = { "Orthographic", "Perspective" };
                if (ImGui::BeginCombo("Projection", projection_type[curr_projection_index])) {
                    for (int i = 0; i < 2; i++) {
                        bool is_selected = (curr_projection_index == i);
                        if (ImGui::Selectable(projection_type[i], is_selected)) {
                            if (component.projection_type != (CameraComponent::ProjectionType)i) {
                                component.projection_type = (CameraComponent::ProjectionType)i;

                                float current_aspect_ratio = component.camera->get_aspect_ratio();
                                component.update_projection(current_aspect_ratio);
                            }
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                if (component.projection_type == CameraComponent::ProjectionType::Orthographic) {
                    bool changed = false;
                    changed |= ImGui::DragFloat("Orthographic Size", &component.orthographic_size, 0.1f, 0.0f, 100.0f);
                    changed |= ImGui::DragFloat("Near Clip", &component.orthographic_near, 0.01f, 0.0f, 100.0f);
                    changed |= ImGui::DragFloat("Far Clip", &component.orthographic_far, 0.01f, 0.0f, 100.0f);

                    if (changed && camera) {
                        if (auto ortho_cam = dynamic_cast<OrthographicCamera*>(camera.get())) {
                            ortho_cam->set_size(component.orthographic_size);
                            ortho_cam->set_near_clip(component.orthographic_near);
                            ortho_cam->set_far_clip(component.orthographic_far);
                        }
                    }
                }

                if (component.projection_type == CameraComponent::ProjectionType::Perspective) {
                    bool changed = false;
                    changed |= ImGui::DragFloat("FOV", &component.perspective_fov, 1.0f, 1.0f, 179.0f);
                    changed |= ImGui::DragFloat("Near Clip", &component.perspective_near, 0.01f, 0.01f, 100.0f);
                    changed |= ImGui::DragFloat("Far Clip", &component.perspective_far, 1.0f, 1.0f, 10000.0f);

                    if (changed && camera) {
                        if (auto persp_cam = dynamic_cast<PerspectiveCamera*>(camera.get())) {
                            persp_cam->set_fov(component.perspective_fov);
                            persp_cam->set_near_clip(component.perspective_near);
                            persp_cam->set_far_clip(component.perspective_far);
                        }
                    }
                }
        });

        draw_component<SpriteRendererComponent>("Sprite Renderer", entity, [](auto& component) {
            ImGui::ColorEdit4("Color", glm::value_ptr(component.color));

            ImGui::Button("Texture", ImVec2(100, 20));
            if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                if (payload->IsDelivery() && payload->Data && payload->DataSize > 0) {
                    const char* path_str = (const char*)payload->Data;
                    std::filesystem::path path = path_str;
                    std::filesystem::path texture_path = std::filesystem::path(g_assets_dir) / path;
                    component.texture_path = texture_path;
                    component.texture = Texture2D::create(texture_path.string());
                }
            }
            ImGui::EndDragDropTarget();
        }

            ImGui::DragFloat("Tiling Factor", &component.tiling_factor, 0.1f, 0.0f, 100.0f);

        });

        draw_component<NativeScriptComponent>("Native Script", entity, [](auto& component) {
                // Display current script name
                std::string display_name = component.script_name.empty() ? "None" : component.script_name;
                ImGui::Text("Script: %s", display_name.c_str());

                // Drag-drop target for scripts
                ImGui::Button("Drop Script Here", ImVec2(200, 20));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        if (payload->IsDelivery() && payload->Data && payload->DataSize > 0) {
                            const char* path_str = (const char*)payload->Data;
                            std::filesystem::path path = path_str;

                            // Extract script name from file path (without extension)
                            std::string script_name = path.stem().string();

                            // Check if script is registered
                            if (ScriptRegistry::get().has_script(script_name)) {
                                component.bind_by_name(script_name);
                                HN_CORE_INFO("Bound script: {0}", script_name);
                            } else {
                                HN_CORE_WARN("Script not registered: {0}", script_name);
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Optional: Dropdown to select from registered scripts
                if (ImGui::BeginCombo("Available Scripts", display_name.c_str())) {
                    auto script_names = ScriptRegistry::get().get_all_script_names();
                    for (const auto& name : script_names) {
                        bool is_selected = (component.script_name == name);
                        if (ImGui::Selectable(name.c_str(), is_selected)) {
                            component.bind_by_name(name);
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            });

    }

}
