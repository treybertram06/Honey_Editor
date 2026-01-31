#include "editor_layer.h"

#include <glm/glm/gtc/type_ptr.hpp>
#include "hnpch.h"
//#include "../assets/scripts/script_registrar.h"
#include "../Honey/vendor/imguizmo/ImGuizmo.h"
#include "Honey/core/settings.h"
#include "Honey/imgui/imgui_utils.h"

#include "Honey/scene/scene_serializer.h"
#include "Honey/utils/platform_utils.h"

#include "Honey/math/math.h"
#include "Honey/renderer/texture_cache.h"
#include "Honey/scripting/script_properties_loader.h"
#include "platform/vulkan/vk_framebuffer.h"
#include "platform/vulkan/vk_texture.h"
#include "scripting/script_loader.h"

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

        new_scene();
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

        Renderer::set_render_target(m_framebuffer);
        Renderer::begin_pass();

        RenderCommand::set_clear_color(m_clear_color);
        RenderCommand::clear();
        //m_framebuffer->clear_attachment_i32(1, -1);

        switch (m_scene_state) {
        case SceneState::edit:
            {
                if (m_viewport_focused)
                    m_editor_camera.on_update(ts);

                m_active_scene->on_update_editor(ts, m_editor_camera);
                on_overlay_render();
                break;
            }
        case SceneState::play:
            {
                m_gizmo_type = -1;
                m_active_scene->on_update_runtime(ts);
                on_overlay_render();
                break;
            }
        }

        Renderer::end_pass();
        Renderer::set_render_target(nullptr); // default back to main window for the rest of the frame
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
                if (ImGui::MenuItem("Save", "Ctrl+S")) {
                    save_current_scene();
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
            if (ImGui::BeginMenu("Scripts")) {
                if (ImGui::MenuItem("Build Scripts")) {
                    ScriptLoader::get().unload_library();

                    int result = std::system("cmake --build . --target HoneyScripts");
                    if (result == 0) {
                        HN_CORE_INFO("Scripts built successfully, reloading...");
                        //print current working directory
                        std::filesystem::path current_path = std::filesystem::current_path();
                        HN_CORE_INFO("Current working directory: {0}", current_path.string());
                        ScriptLoader::get().reload_library("assets/scripts/libHoneyScripts.so");
                    } else {
                        HN_CORE_ERROR("Script build failed with code: {0}", result);
                    }
                }
                if (ImGui::MenuItem("Reload Scripts")) {
                    ScriptLoader::get().reload_library("assets/scripts/libHoneyScripts.so");
                }
                if (ImGui::MenuItem("Invalidate Lua Script Property Cache")) {
                    ScriptPropertiesLoader::invalidate_all();
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
            auto& renderer = Settings::get().renderer;

            if (ImGui::ColorEdit4("Clear Color", glm::value_ptr(renderer.clear_color))) {
                m_clear_color = renderer.clear_color;
            }

            if (ImGui::Checkbox("Wireframe Mode", &renderer.wireframe)) {
                RenderCommand::set_wireframe(renderer.wireframe);
            }

            if (ImGui::Checkbox("Depth Test", &renderer.depth_test)) {
                RenderCommand::set_depth_test(renderer.depth_test);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Depth Write", &renderer.depth_write)) {
                RenderCommand::set_depth_write(renderer.depth_write);
            }

            if (ImGui::Checkbox("Face Culling", &renderer.face_culling)) {
                // RenderCommand::set_face_culling(renderer.face_culling);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Blending", &renderer.blending)) {
                RenderCommand::set_blend(renderer.blending);
            }

            // Texture filter combo
            {
                // Names must match the enum ordering
                static const char* filter_names[] = {
                    "Nearest",
                    "Linear",
                    "Anisotropic"
                };

                int current = static_cast<int>(renderer.texture_filter);
                if (ImGui::Combo("Texture Filter", &current, filter_names, IM_ARRAYSIZE(filter_names))) {
                    renderer.texture_filter =
                        static_cast<RendererSettings::TextureFilter>(current);

                    auto& window = Application::get().get_window();
                    auto ctx = window.get_context(); // Not a huge fan of all this, but it does work!
                    if (ctx)
                        ctx->refresh_all_texture_samplers();

                    HN_CORE_INFO("Texture filter changed to {}", current);
                }
            }

            if (renderer.texture_filter == RendererSettings::TextureFilter::anisotropic) {
                static const float af_values[]  = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
                static const char* af_labels[]  = { "1x", "2x", "4x", "8x", "16x" };
                int current_index = 0;

                // Find closest entry to current value
                for (int i = 0; i < (int)std::size(af_values); ++i) {
                    if (std::abs(renderer.anisotropic_filtering_level - af_values[i]) < 0.5f) {
                        current_index = i;
                        break;
                    }
                }

                if (ImGui::Combo("Anisotropy", &current_index, af_labels, IM_ARRAYSIZE(af_labels))) {
                    renderer.anisotropic_filtering_level = af_values[current_index];
                    // Recreate samplers so the new level takes effect
                    TextureCache::get().recreate_all_samplers();
                }
            }

            if (ImGui::Checkbox("Show Physics Colliders", &renderer.show_physics_debug_draw)) {
                m_show_physics_colliders = renderer.show_physics_debug_draw;
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("V-Sync", &renderer.vsync)) {
                //RenderCommand::set_vsync(renderer.vsync);
            }



        }
/*
        // Debug Section
        if (ImGui::CollapsingHeader("Debug")) {
            static bool show_normals = false;
            static bool show_bounding_boxes = false;
            static bool show_grid = false;

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
*/
        ImGui::End();

        ImGui::Begin("Physics Debug Panel");

        // Physics Settings Section
        if (ImGui::CollapsingHeader("Physics Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& physics = Settings::get().physics;

            ImGui::Checkbox("Enable Physics", &physics.enabled);

            ImGui::DragInt("Substeps", &physics.substeps, 1, 1, 20);

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

        ImTextureID imgui_tex_id = m_framebuffer->get_imgui_color_texture_id(0);

        UI::Image(imgui_tex_id,
                         ImVec2(m_viewport_size.x, m_viewport_size.y),
                         ImVec2(0.0f, 0.0f),   // uv0
                         ImVec2(1.0f, 1.0f));  // uv1

        if (ImGui::BeginDragDropTarget()) { /// Talk about this maybe as an example of an issue when working with pointers?
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                if (payload->IsDelivery() && payload->Data && payload->DataSize > 0) {
                    const char* path_str = (const char*)payload->Data;
                    std::filesystem::path path = path_str;

                    if (path.extension() == ".hns")
                        open_scene(g_assets_dir / path);
                    else if (path.extension() == ".hnp")
                        m_active_scene->add_prefab_to_scene(g_assets_dir / path);
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

                transform_component.collider_dirty = true;
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

        if (UI::ImageButton("play_stop_button", icon->get_imgui_texture_id(), ImVec2(size, size))) {
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
            else if (control) { save_current_scene(); handled = true; }
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


        //Scene commands
        case KeyCode::D:
            if (control) { on_duplicate_entity(); handled = true; }
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

    void EditorLayer::on_overlay_render() {

        if (m_scene_state == SceneState::play) {
            Entity camera_entity = m_active_scene->get_primary_camera();
            if (!camera_entity.is_valid())
                return;
            Renderer2D::begin_scene(*camera_entity.get_component<CameraComponent>().camera, camera_entity.get_component<TransformComponent>().get_transform());
        } else {
            Renderer2D::begin_scene(m_editor_camera);
        }

        if (m_show_physics_colliders) {
            { // Box colliders
                auto view = m_active_scene->get_all_entities_with<TransformComponent, BoxCollider2DComponent>();
                for (auto entity : view) {
                    auto [tc, bc2d] = view.get<TransformComponent, BoxCollider2DComponent>(entity);

                    Entity e{ entity, m_active_scene.get() };

                    glm::mat4 transform =
                        e.get_world_transform() *
                        glm::translate(glm::mat4(1.0f), glm::vec3(bc2d.offset, 0.001f)) *
                        glm::scale(glm::mat4(1.0f),
                            glm::vec3(bc2d.size * 2.0f, 1.0f));

                    Renderer2D::draw_rect(transform, glm::vec4(0, 1, 0, 1));
                }
            }
            { // Circle colliders
                auto view = m_active_scene->get_all_entities_with<TransformComponent, CircleCollider2DComponent>();
                for (auto entity : view) {
                    auto [tc, cc2d] = view.get<TransformComponent, CircleCollider2DComponent>(entity);

                    Entity e{ entity, m_active_scene.get() };

                    glm::mat4 transform =
                        e.get_world_transform() *
                        glm::translate(glm::mat4(1.0f), glm::vec3(cc2d.offset, 0.001f)) *
                        glm::scale(glm::mat4(1.0f),
                            glm::vec3(cc2d.radius * 2.0f));

                    Renderer2D::draw_circle(transform, glm::vec4(0, 1, 0, 1), 0.05f);
                }
            }
        }

        Renderer2D::end_scene();

    }


    void EditorLayer::new_scene() {
        m_editor_scene = CreateRef<Scene>();
        m_active_scene = m_editor_scene;
        m_active_scene->on_viewport_resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);
        m_scene_hierarchy_panel.set_context(m_active_scene);
    }

    void EditorLayer::open_scene() {
        std::string path = FileDialogs::open_file("Honey Scene (*.hns)\0*.hns\0");
        if (!path.empty())
            open_scene(path);
    }

    void EditorLayer::open_scene(const std::filesystem::path& path) {
        if (m_scene_state != SceneState::edit)
            on_scene_stop();

        if (path.extension() != ".hns") {
            HN_CORE_WARN("File {0} is not a Honey Scene file!", path.string());
            return;
        }

        m_scene_filepath = path;
        Ref<Scene> new_scene = CreateRef<Scene>();
        SceneSerializer serializer(new_scene);
        if (serializer.deserialize(path)) {
            m_scene_hierarchy_panel.set_selected_entity({});
            m_editor_scene = new_scene;
            m_editor_scene->on_viewport_resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);

            m_active_scene = m_editor_scene;
            m_scene_hierarchy_panel.set_context(m_active_scene);
        }

        //m_active_scene = CreateRef<Scene>();
        //m_active_scene->on_viewport_resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);
        //m_scene_hierarchy_panel.set_context(m_active_scene);
//
        //SceneSerializer serializer(m_active_scene);
        //serializer.deserialize(path);
    }

    void EditorLayer::save_scene_as(std::filesystem::path path) {
        if (path.empty())
            path = FileDialogs::save_file("Honey Scene (*.hns)\0");
        else
            HN_CORE_INFO("Saving scene as {0}", path.string());
        if (!path.empty()) {
            SceneSerializer serializer(m_active_scene);
            serializer.serialize(path);
        }
    }

    void EditorLayer::save_current_scene() {
        save_scene_as(m_scene_filepath);
    }

    // Note to self, editor scene is null in some cases so the copy doesnt work, test from fresh scene on boot, ctrlN scene, and saved scene to confirm

    void EditorLayer::on_scene_play() {
        m_scene_state = SceneState::play;

        m_active_scene = Scene::copy(m_editor_scene);
        m_active_scene->on_viewport_resize((uint32_t)m_viewport_size.x, (uint32_t)m_viewport_size.y);
        m_active_scene->on_runtime_start();
        m_scene_hierarchy_panel.set_context(m_active_scene);
    }

    void EditorLayer::on_scene_stop() {
        m_scene_state = SceneState::edit;

        m_active_scene->on_runtime_stop();
        m_active_scene = m_editor_scene;
        m_scene_hierarchy_panel.set_context(m_active_scene);
    }

    void EditorLayer::on_duplicate_entity() {
        if (m_scene_state != SceneState::edit) return;

        Entity entity = m_scene_hierarchy_panel.get_selected_entity();
        if (entity)
            m_editor_scene->duplicate_entity(entity);
    }
}
