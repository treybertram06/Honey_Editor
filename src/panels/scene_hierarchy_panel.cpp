#include "scene_hierarchy_panel.h"

#include <filesystem>
#include <imgui.h>

#include "imgui_internal.h"
#include "glm/gtc/type_ptr.inl"
#include "Honey/renderer/texture.h"
#include "Honey/scene/scene_serializer.h"
#include "Honey/scene/script_registry.h"
#include "Honey/scripting/script_engine.h"
#include "Honey/scripting/script_properties_loader.h"
#include "Honey/scripting/script_properties_writer.h"
//#include "Honey/scripting/mono_script_engine.h"


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

                // Only draw entities that are not children (i.e., have no parent)
                if (!current_entity.has_component<RelationshipComponent>() ||
                    current_entity.get_component<RelationshipComponent>().parent == entt::null) {
                    draw_entity_node(current_entity);
                    }
            }
        }

        ImVec2 pos  = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();

        ImRect window_rect(
            pos,
            ImVec2(pos.x + size.x, pos.y + size.y)
        );

        if (ImGui::BeginDragDropTargetCustom(
                window_rect,
                ImGui::GetID("SceneHierarchyRoot"))) {

            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_ENTITY")) {
                if (payload->Data && payload->DataSize == sizeof(entt::entity)) {
                    entt::entity dropped_handle = *(entt::entity*)payload->Data;
                    Entity dropped_entity{ dropped_handle, m_context.get() };

                    if (dropped_entity.is_valid()) {
                        dropped_entity.set_parent(Entity{});
                    }
                }
            }
            ImGui::EndDragDropTarget();
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

    bool SceneHierarchyPanel::is_descendant(Entity entity, Entity ancestor) const {
        if (!entity.is_valid() || !ancestor.is_valid())
            return false;

        Entity current = ancestor;
        while (current.has_parent()) {
            current = current.get_parent();
            if (current == entity)
                return true;
        }
        return false;
    }

    void SceneHierarchyPanel::draw_entity_node(Entity entity) {
        auto& tag = entity.get_component<TagComponent>();
        bool has_children = entity.has_component<RelationshipComponent>() &&
                            !entity.get_component<RelationshipComponent>().children.empty();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ((m_selected_entity == entity) ? ImGuiTreeNodeFlags_Selected : 0);
        if (!has_children)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity.get_handle(), flags, "%s", tag.tag.c_str());

        if (ImGui::IsItemClicked())
            m_selected_entity = entity;

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            entt::entity handle = entity.get_handle();
            ImGui::SetDragDropPayload(
                "SCENE_ENTITY",
                &handle,
                sizeof(entt::entity)
            );
            ImGui::TextUnformatted(tag.tag.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_ENTITY")) {
                if (payload->Data && payload->DataSize == sizeof(entt::entity)) {
                    entt::entity dropped_handle = *(entt::entity*)payload->Data;
                    Entity dropped_entity{ dropped_handle, m_context.get() };

                    // Safety checks
                    if (dropped_entity.is_valid() &&
                        dropped_entity != entity &&
                        !is_descendant(dropped_entity, entity)) {

                        dropped_entity.set_parent(entity);
                        }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem()) {

            if (ImGui::MenuItem("Delete Entity")) {
                if (m_selected_entity == entity)
                    m_selected_entity = {};
                m_context->destroy_entity(entity);
                ImGui::EndPopup();
                if (opened && has_children)
                    ImGui::TreePop();
                return;
            }

            if (ImGui::MenuItem("Create Child")) {
                Entity child = m_context->create_child_for(entity, "Child Entity");
                m_selected_entity = child;
            }

            if (ImGui::MenuItem("Create Prefab")) {
                std::string default_path =
                    (g_assets_dir / "prefabs" / (tag.tag + ".hnp")).string();
                if (!default_path.empty()) {
                    SceneSerializer serializer(m_context);
                    serializer.serialize_entity_prefab(entity, default_path);
                } else {
                    ImGui::OpenPopup("Save Prefab As...");
                    HN_CORE_WARN("No default path for prefab found and no custom path implementation yet");
                }
            }

            if (ImGui::MenuItem("Duplicate Entity")) {
                m_context->duplicate_entity(entity);
            }

            ImGui::EndPopup();
        }

        // Recursively draw children
        if (opened && has_children) {
            auto& rel = entity.get_component<RelationshipComponent>();
            for (auto child_handle : rel.children) {
                Entity child_entity = { child_handle, m_context.get() };
                if (child_entity.is_valid())
                    draw_entity_node(child_entity);
            }
            ImGui::TreePop();
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

        float line_height = ImGui::GetFontSize() + GImGui->Style.FramePadding.y * 2.0f;
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

        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_FramePadding;// | ImGuiTreeNodeFlags_AllowItemOverlap;

        if (entity.has_component<T>()) {

            auto& component = entity.get_component<T>();
            ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
            float line_height = ImGui::GetFontSize() + GImGui->Style.FramePadding.y * 2.0f;
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

            if (!m_selected_entity.has_component<TransformComponent>()) {
                if (ImGui::MenuItem("Transform Component")) {
                    entity.add_component<TransformComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!m_selected_entity.has_component<SpriteRendererComponent>()) {
                if (ImGui::MenuItem("Sprite Renderer")) {
                    entity.add_component<SpriteRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!m_selected_entity.has_component<CircleRendererComponent>()) {
                if (ImGui::MenuItem("Circle Renderer")) {
                    entity.add_component<CircleRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!m_selected_entity.has_component<LineRendererComponent>()) {
                if (ImGui::MenuItem("Line Renderer")) {
                    entity.add_component<LineRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!m_selected_entity.has_component<CameraComponent>()) {
                if (ImGui::MenuItem("Camera Component")) {
                    entity.add_component<CameraComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!m_selected_entity.has_component<NativeScriptComponent>()) {
                if (ImGui::MenuItem("Native Script Component")) {
                    entity.add_component<NativeScriptComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!m_selected_entity.has_component<ScriptComponent>()) {
                if (ImGui::MenuItem("Lua Script Component")) {
                    entity.add_component<ScriptComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!m_selected_entity.has_component<BoxCollider2DComponent>()) {
                if (ImGui::MenuItem("Box Collider 2D")) {
                    entity.add_component<BoxCollider2DComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!m_selected_entity.has_component<CircleCollider2DComponent>()) {
                if (ImGui::MenuItem("Circle Collider 2D")) {
                    entity.add_component<CircleCollider2DComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            const bool has_parent = entity.has_parent();

            if (has_parent) {
                ImGui::BeginDisabled();
            }

            if (!m_selected_entity.has_component<Rigidbody2DComponent>()) {
                if (ImGui::MenuItem("Rigidbody 2D")) {
                    entity.add_component<Rigidbody2DComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (has_parent) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip(
                        "Rigidbody2D entities must be world-space roots.\n"
                        "Remove the parent before adding Rigidbody2D."
                    );
                }
            }

            ImGui::EndPopup();
        }

        ImGui::PopItemWidth();


        draw_component<TransformComponent>("Transform", entity, [](auto& component) {
            draw_vec3_control("Translation", component.translation);
                glm::vec3 rotation = glm::degrees(component.rotation);
                draw_vec3_control("Rotation", rotation);
                component.rotation = glm::radians(rotation);

                glm::vec3 old_scale = component.scale;
                draw_vec3_control("Scale", component.scale, 1.0f);

                if (component.scale != old_scale) {
                    component.collider_dirty = true;
                }
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

            ImGui::Checkbox("Primary", &component.primary);
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

                        Ref<Texture2D> texture = Texture2D::create(texture_path.string());

                        component.sprite = Sprite::create_from_texture(texture, 8.0f);
                        component.sprite_path = texture_path;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (component.sprite) {
                    Sprite& sprite = *component.sprite;

                    float ppu = sprite.get_pixels_per_unit();
                    if (ImGui::DragFloat("Pixels Per Unit", &ppu, 0.1f, 0.01f, 1000.0f)) {
                        sprite.set_pixels_per_unit(ppu);
                    }

                    glm::vec2 pivot = sprite.get_pivot();
                    if (ImGui::DragFloat2("Pivot", glm::value_ptr(pivot), 0.01f, 0.0f, 1.0f)) {
                        // Clamp to [0,1] to avoid weird values
                        pivot.x = glm::clamp(pivot.x, 0.0f, 1.0f);
                        pivot.y = glm::clamp(pivot.y, 0.0f, 1.0f);
                        sprite.set_pivot(pivot);
                    }

                    if (ImGui::TreeNodeEx("Sprite Info", ImGuiTreeNodeFlags_DefaultOpen)) {
                        glm::ivec2 px_min  = sprite.get_pixel_min();
                        glm::ivec2 px_size = sprite.get_pixel_size();
                        glm::vec2  world_size = sprite.get_world_size();

                        ImGui::Text("Pixel Rect: min=(%d, %d), size=(%d, %d)",
                                    px_min.x, px_min.y, px_size.x, px_size.y);
                        ImGui::Text("World Size: (%.3f, %.3f)",
                                    world_size.x, world_size.y);
                        ImGui::TreePop();
                    }
                } else {
                    ImGui::TextDisabled("No sprite assigned");
                }
        });

        draw_component<CircleRendererComponent>("Circle Renderer", entity, [](auto& component) {
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

            ImGui::DragFloat("Thickness", &component.thickness, 0.025f, 0.0f, 1.0f);
            ImGui::DragFloat("Fade", &component.fade, 0.001f, 0.0f, 1.0f);
        });

        draw_component<LineRendererComponent>("Line Renderer", entity, [](auto& component) {
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

            ImGui::DragFloat("Fade", &component.fade, 0.001f, 0.0f, 1.0f);
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

        draw_component<ScriptComponent>("Lua Script", entity, [&](auto& component) {
            // Display current script name
            std::string display_name = component.script_name.empty() ? "None" : component.script_name;
            ImGui::Text("Script: %s", display_name.c_str());

            // Drag & Drop to assign script
            ImGui::Button("Drop Lua Script Here", ImVec2(200, 20));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    const char* path_str = (const char*)payload->Data;
                    std::filesystem::path path = path_str;

                    if (path.extension() == ".lua") {
                        component.script_name = path.stem().string();
                        HN_CORE_INFO("Lua script assigned: {}", component.script_name);
                    } else {
                        HN_CORE_WARN("Not a Lua script: {}", path.string());
                    }
                }

                ImGui::EndDragDropTarget();
            }

            // Dropdown script selection
            if (ImGui::BeginCombo("Available Lua Scripts", display_name.c_str())) {
                const std::filesystem::path script_dir = g_assets_dir / "scripts";

                if (std::filesystem::exists(script_dir)) {
                    for (auto& entry : std::filesystem::directory_iterator(script_dir)) {
                        if (entry.path().extension() == ".lua") {
                            std::string name = entry.path().stem().string();
                            bool is_selected = (component.script_name == name);

                            if (ImGui::Selectable(name.c_str(), is_selected))
                                component.script_name = name;

                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                    }
                }

                ImGui::EndCombo();
            }

            if (entity.get_scene()) {
                auto defaults = ScriptPropertiesLoader::load_from_file(component.script_name);

                if (!defaults.empty()) {
                    ImGui::Separator();
                    ImGui::Text("Script Properties");

                    for (auto& [name, default_value] : defaults) {
                        ImGui::PushID(name.c_str());

                        // Check override
                        auto it = component.property_overrides.find(name);
                        bool has_override = it != component.property_overrides.end();

                        if (std::holds_alternative<float>(default_value)) {
                            float value = has_override
                                ? std::get<float>(it->second)
                                : std::get<float>(default_value);

                            if (ImGui::DragFloat(name.c_str(), &value, 0.1f)) {
                                component.property_overrides[name] = value;
                            }

                        } else if (std::holds_alternative<bool>(default_value)) {
                            bool value = has_override
                                ? std::get<bool>(it->second)
                                : std::get<bool>(default_value);

                            if (ImGui::Checkbox(name.c_str(), &value)) {
                                component.property_overrides[name] = value;
                            }

                        } else if (std::holds_alternative<std::string>(default_value)) {
                            std::string value = has_override
                                ? std::get<std::string>(it->second)
                                : std::get<std::string>(default_value);

                            char buffer[256];
                            memset(buffer, 0, sizeof(buffer));
                            strncpy(buffer, value.c_str(), sizeof(buffer) - 1);

                            if (ImGui::InputText(name.c_str(), buffer, sizeof(buffer))) {
                                component.property_overrides[name] = std::string(buffer);
                            }
                        }

                        // Clear override button
                        if (has_override) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Reset")) {
                                component.property_overrides.erase(name);
                            }
                        }

                        ImGui::PopID();
                    }
                }
            }

            if (!component.property_overrides.empty()) {
                ImGui::Separator();

                if (ImGui::Button("Apply as Defaults")) {
                    std::string error;
                    if (ScriptPropertiesWriter::write_defaults(
                            component.script_name,
                            component.property_overrides,
                            &error)) {

                        component.property_overrides.clear();
                        ScriptPropertiesLoader::invalidate_cache(component.script_name);
                    } else {
                        HN_CORE_ERROR("Failed to write Lua defaults: {}", error);
                    }
                }
            }
        });

        draw_component<Rigidbody2DComponent>("Rigidbody2D", entity, [](auto& component) {
            const char* body_type[] = { "Static", "Dynamic", "Kinematic" };
            const char* curr_body_type = body_type[(int)component.body_type];
            if (ImGui::BeginCombo("Body Type", curr_body_type)) {
                for (int i = 0; i < 3; i++) {
                    bool is_selected = (curr_body_type == body_type[i]);
                    if (ImGui::Selectable(body_type[i], is_selected)) {
                        if (component.body_type != (Rigidbody2DComponent::BodyType)i) {
                            component.body_type = (Rigidbody2DComponent::BodyType)i;
                        }
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Checkbox("Fixed Rotation", &component.fixed_rotation);
        });

        draw_component<BoxCollider2DComponent>("Box Collider 2D", entity, [](auto& component) {
            ImGui::DragFloat2("Offset", glm::value_ptr(component.offset), 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat2("Size", glm::value_ptr(component.size), 0.1f, 0.0f, 100.0f);

            ImGui::DragFloat("Density", &component.density, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Friction", &component.friction, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Restitution", &component.restitution, 0.01f, 0.0f, 1.0f);
        });

        draw_component<CircleCollider2DComponent>("Circle Collider 2D", entity, [](auto& component) {
            ImGui::DragFloat2("Offset", glm::value_ptr(component.offset), 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("Radius", &component.radius, 0.1f, 0.0f, 100.0f);

            ImGui::DragFloat("Density", &component.density, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Friction", &component.friction, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Restitution", &component.restitution, 0.01f, 0.0f, 1.0f);
        });

    }
}
