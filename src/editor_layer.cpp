#include "editor_layer.h"

#include <glm/glm/gtc/type_ptr.hpp>
#include "hnpch.h"
#include "../assets/scripts/script_registrar.h"
#include "../Honey/vendor/imguizmo/ImGuizmo.h"

#include "Honey/scene/scene_serializer.h"
#include "Honey/utils/platform_utils.h"

#include "Honey/math/math.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {

    extern const std::filesystem::path g_assets_dir;

    EditorLayer::EditorLayer()
        : Layer("EditorLayer") {}


    void EditorLayer::on_attach() {

        m_icon_play = Texture2D::create("../resources/icons/toolbar/play_button.png");
        m_icon_stop = Texture2D::create("../resources/icons/toolbar/stop_button.png");

        FramebufferSpecification fb_spec;
        fb_spec.attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
        fb_spec.width = 1280;
        fb_spec.height = 720;
        m_framebuffer = Framebuffer::create(fb_spec);

        m_active_scene = CreateRef<Scene>();

        m_scene_hierarchy_panel.set_context(m_active_scene);

        m_editor_camera = EditorCamera(16.0f/9.0f, 45.0f, 0.1f, 1000.0f);




    }

    void EditorLayer::render_camera_selector() {
        if (!ImGui::CollapsingHeader("Camera Selection", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }

        ImGui::Text("Primary Camera:");
        ImGui::Separator();

        std::vector<Entity> camera_entities;
        if (m_active_scene) {
            auto camera_view = m_active_scene->get_registry().view<CameraComponent>();
            for (auto entity_id : camera_view) {
                Entity camera_entity = { entity_id, m_active_scene.get() };
                if (camera_entity.is_valid()) {
                    camera_entities.push_back(camera_entity);
                }
            }
        }

        Entity current_primary_camera = m_active_scene->get_primary_camera();

        static int selected_camera = -1; // Index of currently selected camera

        for (int i = 0; i < camera_entities.size(); ++i) {
            if (current_primary_camera.is_valid() &&
                camera_entities[i].get_handle() == current_primary_camera.get_handle()) {
                selected_camera = i;
                break;
            }
        }

        if (!current_primary_camera.is_valid()) {
            selected_camera = -1;
        }

        for (int i = 0; i < camera_entities.size(); ++i) {
            const std::string& camera_name = camera_entities[i].get_component<TagComponent>().tag;

            if (ImGui::RadioButton(camera_name.c_str(), selected_camera == i)) {
                selected_camera = i;

                m_active_scene->set_primary_camera(camera_entities[i]);

                auto& camera_component = m_active_scene->get_primary_camera().get_component<CameraComponent>();
                float aspect_ratio = (float)m_viewport_size.x / (float)m_viewport_size.y;
                camera_component.get_camera()->set_aspect_ratio(aspect_ratio);
            }

            // Add some spacing between radio buttons
            if (i < camera_entities.size() - 1) {
                ImGui::Spacing();
            }
        }

        ImGui::Separator();

        if (ImGui::Button("Clear Primary Camera")) {
            m_active_scene->clear_primary_camera();
            selected_camera = -1; // Reset selection
            HN_CORE_INFO("Primary camera cleared");
        }

        ImGui::Spacing();

        std::string current_primary = "None";
        if (current_primary_camera.is_valid()) {
            current_primary = current_primary_camera.get_component<TagComponent>().tag;
        }

        ImGui::Text("Current Primary: %s", current_primary.c_str());
        ImGui::Text("Total Cameras: %zu", camera_entities.size());
    }




    void EditorLayer::on_detach() {
    }

    void EditorLayer::on_update(Timestep ts) {
        HN_PROFILE_FUNCTION();

        m_frame_time = ts.get_millis();
        m_framerate_counter.update(ts);
        m_framerate = m_framerate_counter.get_smoothed_fps();

        m_framebuffer->bind();
        RenderCommand::set_clear_color(m_clear_color);
        RenderCommand::clear();
        m_framebuffer->clear_attachment_i32(1, -1);

        switch (m_scene_state) {
            case SceneState::edit:
            {
                if (m_viewport_focused)
                    m_editor_camera.on_update(ts);

                m_active_scene->on_update_editor(ts, m_editor_camera); break;
            }
            case SceneState::play:
            {
                    m_gizmo_type = -1;
                m_active_scene->on_update_runtime(ts); break;
            }
        }

        m_framebuffer->unbind();
    }

    void EditorLayer::on_imgui_render() {
        HN_PROFILE_FUNCTION();

        ImGuiStyle& style = ImGui::GetStyle();
        float min_window_size = style.WindowMinSize.x;
        style.WindowMinSize.x = 370.0f;

        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        style.WindowMinSize.x = min_window_size;


        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New", "Ctrl+N")) {
                    new_scene();
                }
                if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                    open_scene();
                }
                if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
                    save_scene_as();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) {
                    Application::quit();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
                    // Action for Undo
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
                    // Action for Redo
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Cut", "Ctrl+X")) {
                    // Action for Cut
                }
                if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                    // Action for Copy
                }
                if (ImGui::MenuItem("Paste", "Ctrl+V")) {
                    // Action for Paste
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Toggle Debug Panel")) {
                    // Toggle visibility of Debug Panel
                }
                if (ImGui::MenuItem("Reset View")) {
                    // Reset camera or scene view
                }
                if (ImGui::BeginMenu("Set Theme")) {
                    auto* imgui_layer = Application::get().get_imgui_layer();
                    UITheme current_theme = imgui_layer->get_current_theme();

                    for (int i = 0; i < 7; ++i) {
                        UITheme theme = static_cast<UITheme>(i);
                        bool is_selected = (current_theme == theme);

                        if (ImGui::MenuItem(imgui_layer->get_theme_name(theme), nullptr, is_selected)) {
                            imgui_layer->set_theme(theme);
                        }
                    }
                    ImGui::EndMenu();
                }


                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        m_scene_hierarchy_panel.on_imgui_render();
        m_content_browser_panel.on_imgui_render();

        ImGui::Begin("Renderer Debug Panel");

        render_camera_selector();

        // Performance Section
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Frame Rate: %d FPS", m_framerate);
            ImGui::Text("Frame Time: %.3f ms", m_frame_time);
            ImGui::Text("Smoothed FPS: %d", m_framerate_counter.get_smoothed_fps());

            ImGui::Separator();
            auto stats = Renderer2D::get_stats();
            ImGui::Text("Draw Calls: %d", stats.draw_calls);
            ImGui::Text("Quads: %d", stats.quad_count);
            ImGui::Text("Vertices: %d", stats.get_total_vertex_count());
            ImGui::Text("Indices: %d", stats.get_total_index_count());

            if (ImGui::Button("Reset Statistics")) {
                Renderer2D::reset_stats();
            }
        }

        // Renderer Settings Section
        if (ImGui::CollapsingHeader("Renderer Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            static glm::vec4 clear_color = {0.1f, 0.1f, 0.1f, 1.0f};
            if (ImGui::ColorEdit4("Clear Color", glm::value_ptr(clear_color))) {
                m_clear_color = clear_color;
            }

            static bool wireframe_mode = false;
            static bool depth_test = false;
            static bool face_culling = true;
            static bool blending = true;

            if (ImGui::Checkbox("Wireframe Mode", &wireframe_mode)) {
                RenderCommand::set_wireframe(wireframe_mode);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Depth Test", &depth_test)) {
                RenderCommand::set_depth_test(depth_test);
            }

            if (ImGui::Checkbox("Face Culling", &face_culling)) {
                // RenderCommand::set_face_culling(face_culling);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Blending", &blending)) {
                RenderCommand::set_blend(blending);
            }
        }

        // Debug Section
        if (ImGui::CollapsingHeader("Debug")) {
            static bool show_wireframe = false;
            static bool show_normals = false;
            static bool show_bounding_boxes = false;
            static bool show_grid = false;

            if (ImGui::Checkbox("Show Wireframe", &show_wireframe)) {
                RenderCommand::set_wireframe(show_wireframe);
                HN_CORE_INFO("Wireframe change triggered");
            }
            ImGui::SameLine();
            ImGui::Checkbox("Show Normals", &show_normals);

            ImGui::Checkbox("Show Bounding Boxes", &show_bounding_boxes);
            ImGui::SameLine();
            ImGui::Checkbox("Show Grid", &show_grid);

            ImGui::Separator();
            ImGui::Text("Renderer Info");
            ImGui::Text("API: OpenGL"); // You can make this dynamic
            ImGui::Text("Version: 4.6"); // You can query this from OpenGL
        }
        ImGui::End();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport");
        auto viewport_offset = ImGui::GetCursorPos();

        m_viewport_focused = ImGui::IsWindowFocused();
        m_viewport_hovered = ImGui::IsWindowHovered();
        Application::get().get_imgui_layer()->block_events(!m_viewport_focused && !m_viewport_hovered);

        ImVec2 viewport_panel_size = ImGui::GetContentRegionAvail();
        if (m_viewport_size != *((glm::vec2*)&viewport_panel_size)) {
            m_viewport_size = {viewport_panel_size.x, viewport_panel_size.y};
            m_framebuffer->resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);
            m_editor_camera.set_viewport_size(m_viewport_size.x, m_viewport_size.y);
            m_active_scene->on_viewport_resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);

        }

        std::uint32_t texture_id = m_framebuffer->get_color_attachment_renderer_id();
        ImGui::Image(ImTextureID((void*)(intptr_t)texture_id), ImVec2(m_viewport_size.x, m_viewport_size.y), ImVec2(0,1), ImVec2(1,0));

        if (ImGui::BeginDragDropTarget()) { /// Talk about this maybe as an example of an issue when working with pointers?
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                if (payload->IsDelivery() && payload->Data && payload->DataSize > 0) {
                    const char* path_str = (const char*)payload->Data;
                    std::filesystem::path path = path_str;
                    open_scene(g_assets_dir / path);
                }
            }
            ImGui::EndDragDropTarget();
        }

        const ImVec2 imgMin = ImGui::GetItemRectMin();
        const ImVec2 imgMax = ImGui::GetItemRectMax();
        const float  imgW   = imgMax.x - imgMin.x;
        const float  imgH   = imgMax.y - imgMin.y;

        const ImVec2 mouse = ImGui::GetMousePos();
        float localX = mouse.x - imgMin.x;
        float localY = mouse.y - imgMin.y;

        if (localX >= 0.0f && localY >= 0.0f && localX < imgW && localY < imgH) {

            const float sx = (imgW > 0.0f) ? (m_viewport_size.x / imgW) : 1.0f;
            const float sy = (imgH > 0.0f) ? (m_viewport_size.y / imgH) : 1.0f;

            const int mouse_x = (int)std::floor(localX * sx + 0.0001f);
            const int mouse_y = (int)std::floor((imgH - localY) * sy + 0.0001f);

            if (mouse_x >= 0 && mouse_y >= 0 && mouse_x < (int)m_viewport_size.x && mouse_y < (int)m_viewport_size.y) {
                if (m_viewport_hovered) {
                    const int id = m_framebuffer->read_pixel(1, mouse_x, mouse_y);

                    if (id == -1) {
                        m_hovered_entity = Entity{}; // invalid
                    } else {
                        Entity picked{ static_cast<entt::entity>(id), m_active_scene.get() };
                        if (picked.is_valid()) {
                            m_hovered_entity = picked;
                        } else {
                            m_hovered_entity = Entity{};
                        }
                    }
                }
            }
        }

        Entity selected_entity = m_scene_hierarchy_panel.get_selected_entity();
        //Entity camera_entity = m_active_scene->get_primary_camera();

        //if (selected_entity && camera_entity && m_gizmo_type != -1) {
        if (selected_entity && m_gizmo_type != -1) {
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
            const glm::mat4 view = m_editor_camera.get_view_matrix();
            const glm::mat4 projection = m_editor_camera.get_projection_matrix();

            //const bool isOrtho = (dynamic_cast<OrthographicCamera*>(camera) != nullptr);
            const bool isOrtho = false;

            ImGuizmo::SetOrthographic(isOrtho);

            auto& transform_component = selected_entity.get_component<TransformComponent>();
            glm::mat4 transform = transform_component.get_transform();

            bool snap = Input::is_key_pressed(KeyCode::LeftControl);
            float snap_value = 0.5f; // 0.5 metres for translation and scale
            if (m_gizmo_type == ImGuizmo::OPERATION::ROTATE) // 45 degrees for rotation
                snap_value = 45.0f;

            float snap_values[3] = { snap_value, snap_value, snap_value };

            ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                                 (ImGuizmo::OPERATION)m_gizmo_type, ImGuizmo::WORLD,
                                 glm::value_ptr(transform), nullptr, snap ? snap_values : nullptr);

            if (ImGuizmo::IsUsing()) {
                glm::vec3 translation, rotation, scale;
                Math::decompose_transform(transform, translation, rotation, scale);

                glm::vec3 delta_rotation = rotation - transform_component.rotation;
                transform_component.translation = translation;
                transform_component.rotation += delta_rotation;
                transform_component.scale = scale;
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();

        //if (m_viewport_hovered)
        //    ImGui::GetIO().WantCaptureMouse = false;

        ui_toolbar();
    }

    void EditorLayer::ui_toolbar() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        auto& colors = ImGui::GetStyle().Colors;
        auto& button_hovered = colors[ImGuiCol_ButtonHovered];
        auto& button_active = colors[ImGuiCol_ButtonActive];
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(button_hovered.x, button_hovered.y, button_hovered.z, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(button_active.x, button_active.y, button_active.z, 0.5f));

        ImGui::Begin("##toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        float size = ImGui::GetWindowHeight() - 4.0f;
        Ref<Texture2D> icon = m_scene_state == SceneState::edit ? m_icon_play : m_icon_stop;
        ImGui::SameLine((ImGui::GetWindowContentRegionMax().x * 0.5f) - (size * 0.5f));

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (ImGui::ImageButton("play_stop_button", (ImTextureID)icon->get_renderer_id(), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1))) {
            if (m_scene_state == SceneState::edit)
                on_scene_play();
            else if (m_scene_state == SceneState::play)
                on_scene_stop();
        }

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);
        ImGui::End();
    }

    void EditorLayer::on_event(Event &event) {
        m_editor_camera.on_event(event);

        EventDispatcher dispatcher(event);
        dispatcher.dispatch<KeyPressedEvent>(HN_BIND_EVENT_FN(EditorLayer::on_key_pressed));
        dispatcher.dispatch<MouseButtonPressedEvent>(HN_BIND_EVENT_FN(EditorLayer::on_mouse_button_pressed));
    }

    bool EditorLayer::on_key_pressed(KeyPressedEvent& e) {
        if (e.get_repeat_count() > 0)
            return false;

        bool control = Input::is_key_pressed(KeyCode::LeftControl) || Input::is_key_pressed(KeyCode::RightControl);
        bool shift = Input::is_key_pressed(KeyCode::LeftShift) || Input::is_key_pressed(KeyCode::RightShift);
        bool alt = Input::is_key_pressed(KeyCode::LeftAlt) || Input::is_key_pressed(KeyCode::RightAlt);
        bool super = Input::is_key_pressed(KeyCode::LeftSuper) || Input::is_key_pressed(KeyCode::RightSuper);

        bool handled = false;

        switch (e.get_key_code()) {
            //scene file shortcuts
        case KeyCode::S:
            if (control && shift) { save_scene_as(); handled = true; }
            break;

        case KeyCode::O:
            if (control) { open_scene(); handled = true; }
            break;

        case KeyCode::N:
            if (control) { new_scene(); handled = true; }
            break;

        // gizmo selectors
        case KeyCode::Q:
            m_gizmo_type = -1;
            break;

        case KeyCode::W:
            m_gizmo_type = ImGuizmo::OPERATION::TRANSLATE;
            break;

        case KeyCode::E:
            m_gizmo_type = ImGuizmo::OPERATION::ROTATE;
            break;

        case KeyCode::R:
            m_gizmo_type = ImGuizmo::OPERATION::SCALE;
            break;

        default:
            break;
        }

        return handled;
    }

    bool EditorLayer::on_mouse_button_pressed(MouseButtonPressedEvent& e) {
        if (e.get_mouse_button() == MouseButton::Left) {
            if (can_mousepick())
                m_scene_hierarchy_panel.set_selected_entity(m_hovered_entity);
        }
        return false;
    }


    void EditorLayer::new_scene() {
        m_active_scene = CreateRef<Scene>();
        m_active_scene->on_viewport_resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);
        m_scene_hierarchy_panel.set_context(m_active_scene);
    }

    void EditorLayer::open_scene() {
        std::string path = FileDialogs::open_file("Honey Scene (*.hns)\0*.hns\0");
        if (!path.empty())
            open_scene(path);
    }

    void EditorLayer::open_scene(const std::filesystem::path& path) {
        m_active_scene = CreateRef<Scene>();
        m_active_scene->on_viewport_resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);
        m_scene_hierarchy_panel.set_context(m_active_scene);

        SceneSerializer serializer(m_active_scene);
        serializer.deserialize(path);
    }
    
    void EditorLayer::save_scene_as() {
        std::string path = FileDialogs::save_file("Honey Scene (*.hns)\0*.hns\0");
        if (!path.empty()) {
            SceneSerializer serializer(m_active_scene);
            serializer.serialize(path);
        }
    }

    void EditorLayer::on_scene_play() {
        m_scene_state = SceneState::play;
    }

    void EditorLayer::on_scene_stop() {
        m_scene_state = SceneState::edit;
    }
}
