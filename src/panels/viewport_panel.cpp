#include "hnpch.h"
#include "viewport_panel.h"
#include "Honey/core/settings.h"
#include "../editor_layer.h"

#include <imgui.h>

#include "glm/gtc/type_ptr.hpp"
#include "Honey/math/math.h"
#include "Honey/ui/imgui_utils.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {

    ViewportPanel::ViewportPanel() {}

    void ViewportPanel::on_imgui_render(EditorLayer& editor) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport");
        auto viewport_offset = ImGui::GetCursorPos();

        editor.m_viewport_focused = ImGui::IsWindowFocused();
        editor.m_viewport_hovered = ImGui::IsWindowHovered();
        Application::get().get_imgui_layer()->block_events(!editor.m_viewport_focused && !editor.m_viewport_hovered);

        ImVec2 viewport_panel_size = ImGui::GetContentRegionAvail();
        if (editor.m_viewport_size != *((glm::vec2*)&viewport_panel_size)) {
            editor.m_pending_viewport_size   = {viewport_panel_size.x, viewport_panel_size.y};
            editor.m_viewport_resize_pending = true;
        }

        ImTextureID imgui_tex_id = editor.m_scene_viewport_renderer.get_imgui_texture_id();

        UI::Image(imgui_tex_id,
                         ImVec2(editor.m_viewport_size.x, editor.m_viewport_size.y),
                         ImVec2(0.0f, 0.0f),   // uv0
                         ImVec2(1.0f, 1.0f));  // uv1

        if (ImGui::BeginDragDropTarget()) { /// Talk about this maybe as an example of an issue when working with pointers?
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                if (payload->IsDelivery() && payload->Data && payload->DataSize > 0) {
                    const char* path_str = (const char*)payload->Data;
                    std::filesystem::path path = path_str;

                    if (path.extension() == ".hns")
                        editor.open_scene(asset_root / path);
                    else if (path.extension() == ".hnp")
                        editor.m_active_scene->add_prefab_to_scene((asset_root / path).string());
                    else if (path.extension() == ".glb" || path.extension() == ".gltf") {
                        auto handle = load_gltf_scene_tree_async(asset_root / path);
                        editor.m_pending_gltf_loads.push_back({handle, asset_root / path});
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        editor.m_viewport_img_min = ImGui::GetItemRectMin();
        editor.m_viewport_img_max = ImGui::GetItemRectMax();

        Entity selected_entity = editor.m_scene_hierarchy_panel.get_selected_entity();
        //Entity camera_entity = editor.m_active_scene->get_primary_camera();
        static bool s_was_gizmo_using = false;

        //if (selected_entity && camera_entity && editor.m_gizmo_type != -1) {
        if (selected_entity && editor.m_gizmo_type != -1) {
            const ImVec2 bottom_left = ImGui::GetItemRectMin();
            const ImVec2 top_right = ImGui::GetItemRectMax();
            const float width  = top_right.x - bottom_left.x;
            const float height = top_right.y - bottom_left.y;

            if (width <= 1.0f || height <= 1.0f) return;

            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(bottom_left.x, bottom_left.y, width, height);

            //auto camera = camera_entity.get_component<CameraComponent>().get_camera();
            //auto& camera_transform = camera_entity.get_component<TransformComponent>();

            //const glm::mat4 view = glm::inverse(camera_transform.get_transform());
            //const glm::mat4 projection = camera->get_projection_matrix();
            const glm::mat4 view = editor.m_editor_camera.get_view_matrix();
            const glm::mat4 projection = editor.m_editor_camera.get_projection_matrix();

            //const bool isOrtho = (dynamic_cast<OrthographicCamera*>(camera) != nullptr);
            const bool isOrtho = false;

            ImGuizmo::SetOrthographic(isOrtho);

            auto& transform_component = selected_entity.get_component<TransformComponent>();
            glm::mat4 transform = transform_component.world;

            bool snap = Input::is_key_pressed(KeyCode::LeftControl);
            float snap_value = 0.5f; // 0.5 metres for translation and scale
            if (editor.m_gizmo_type == ImGuizmo::OPERATION::ROTATE) // 45 degrees for rotation
                snap_value = 45.0f;

            float snap_values[3] = { snap_value, snap_value, snap_value };

            ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                                 (ImGuizmo::OPERATION)editor.m_gizmo_type, ImGuizmo::WORLD,
                                 glm::value_ptr(transform), nullptr, snap ? snap_values : nullptr);

            const bool gizmo_using = ImGuizmo::IsUsing();
            if (gizmo_using) {
                // Convert the manipulated world matrix back to local space before writing to TransformComponent
                glm::mat4 local_mat = transform;
                if (selected_entity.has_parent())
                    local_mat = glm::inverse(selected_entity.get_parent().get_world_transform()) * transform;

                glm::vec3 translation, rotation, scale;
                Math::decompose_transform(local_mat, translation, rotation, scale);

                glm::vec3 delta_rotation = rotation - transform_component.rotation;
                transform_component.translation = translation;
                transform_component.rotation += delta_rotation;
                transform_component.scale = scale;

                transform_component.dirty = true;
                transform_component.collider_dirty = true;
            }

            if (s_was_gizmo_using && !gizmo_using) {
                if (editor.m_scene_state == EditorLayer::SceneState::edit && editor.m_editor_scene)
                    editor.m_editor_scene->mark_dirty();
            }

            s_was_gizmo_using = gizmo_using;
        } else {
            s_was_gizmo_using = false;
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

}
