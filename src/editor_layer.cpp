#include "editor_layer.h"

#include <glm/glm/gtc/type_ptr.hpp>
#include "hnpch.h"
//#include "../assets/scripts/script_registrar.h"
#include "../Honey/vendor/imguizmo/ImGuizmo.h"
#include "Honey/core/settings.h"
#include "../Honey/engine/src/Honey/ui/imgui_utils.h"

#include "Honey/scene/scene_serializer.h"
#include "gltf_entity_builder.h"
#include "Honey/scene/cloth_system.h"
#include "Honey/utils/platform_utils.h"

#include "Honey/math/math.h"
#include "Honey/renderer/texture_cache.h"
#include "scripting/script_loader.h"
#include "../Honey/engine/src/Honey/scripting/csharp_script_engine.h"

#include <cmath>
#include <glm/gtc/constants.hpp>


static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {
    extern const std::filesystem::path g_assets_dir;

    EditorLayer::EditorLayer()
        : Layer("EditorLayer") {}

    void EditorLayer::on_attach() {
        m_icon_play         = Texture2D::create_async("../resources/icons/toolbar/play_button.png");
        m_icon_stop         = Texture2D::create_async("../resources/icons/toolbar/stop_button.png");
        m_icon_pause        = Texture2D::create_async("../resources/icons/toolbar/pause_button.png");
        m_icon_simulate     = Texture2D::create_async("../resources/icons/toolbar/simulate_button.png");

        m_scene_hierarchy_panel.set_notification_center(&m_notification_center);

        new_scene();
        m_editor_camera = EditorCamera(16.0f/9.0f, 45.0f, 0.1f, 1000.0f);
        m_scene_viewport_renderer.initialize();
        m_scene_viewport_renderer.resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);

    }

    bool EditorLayer::update_scene_for_current_state(Timestep ts) {
        HN_PROFILE_FUNCTION();

        switch (m_scene_state) {
        case SceneState::edit:
            {
                if (m_viewport_focused)
                    m_editor_camera.on_update(ts);

                m_active_scene->on_update_editor(ts, m_editor_camera);
                return true;
            }
        case SceneState::play:
            {
                m_gizmo_type = -1;
                m_active_scene->on_update_runtime(ts, paused);
                return m_active_scene->get_primary_camera().is_valid();
            }
        case SceneState::simulate:
            {
                m_gizmo_type = -1;

                if (m_viewport_focused)
                    m_editor_camera.on_update(ts);

                m_active_scene->on_update_simulation(ts, m_editor_camera, paused);
                return true;
            }
        default:
            return false;
        }
    }

    SceneViewportRenderContext EditorLayer::build_scene_viewport_render_context(Timestep ts) {
        SceneViewportRenderContext context{};
        context.scene = m_active_scene.get();
        context.timestep = ts;

        if (m_scene_state == SceneState::play) {
            Entity camera_entity = m_active_scene ? m_active_scene->get_primary_camera() : Entity{};
            if (camera_entity.is_valid()) {
                auto& cc = camera_entity.get_component<CameraComponent>();
                auto& tc = camera_entity.get_component<TransformComponent>();

                if (Camera* primary_camera = cc.get_camera()) {
                    const glm::mat4 transform = camera_entity.get_world_transform();
                    context.view = glm::inverse(transform);
                    context.projection = primary_camera->get_projection_matrix();
                    context.camera_position = tc.translation;
                    context.scene_is_runtime_camera = true;
                }
            }

            context.post_scene_overlay_render = [this]() { on_overlay_render(); };
            return context;
        }

        context.view = m_editor_camera.get_view_matrix();
        context.projection = m_editor_camera.get_projection_matrix();
        context.camera_position = m_editor_camera.get_position();
        if (m_scene_state == SceneState::simulate)
            context.post_scene_overlay_render = [this]() { on_overlay_render(); };
        return context;
    }





    void EditorLayer::on_detach() {
        HN_CORE_INFO("EditorLayer Detached");

        auto& app = Application::get();
        auto& window = app.get_window();
        auto* ctx = window.get_context();
        if (ctx)
            ctx->wait_idle();

        m_scene_viewport_renderer.shutdown();
        m_active_scene.reset();
        m_icon_play.reset();
        m_icon_stop.reset();
    }

    void EditorLayer::on_update(Timestep ts) {
        HN_PROFILE_FUNCTION();

        // Poll pending async glTF loads and spawn entities when ready.
        {
            HN_PROFILE_SCOPE("PollPendingGltfLoads");
            for (auto it = m_pending_gltf_loads.begin(); it != m_pending_gltf_loads.end(); ) {
                if (it->handle->done.load(std::memory_order_acquire)) {
                    if (!it->handle->failed.load(std::memory_order_acquire)) {
                        Entity spawned = spawn_gltf_scene_tree(*m_active_scene, it->handle->tree, it->source_path);
                        m_scene_hierarchy_panel.set_selected_entity(spawned);
                    } else {
                        HN_WARN("Async glTF load failed: {}", it->source_path.string());
                    }
                    it = m_pending_gltf_loads.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Apply any viewport resize that was detected in the previous frame's on_imgui_render.
        // Done here, before any frame graph recording, so that old framebuffer Vulkan objects
        // are destroyed before the CB records any render passes against them.
        if (m_viewport_resize_pending) {
            m_viewport_resize_pending = false;
            m_viewport_size = m_pending_viewport_size;
            m_scene_viewport_renderer.resize((uint32_t)m_viewport_size.x, (uint32_t)m_viewport_size.y);
            m_editor_camera.set_viewport_size(m_viewport_size.x, m_viewport_size.y);
            if (m_active_scene)
                m_active_scene->on_viewport_resize((uint32_t)m_viewport_size.x, (uint32_t)m_viewport_size.y);
        }

        m_frame_time = ts.get_millis();
        m_framerate_counter.update(ts);
        m_framerate = m_framerate_counter.get_smoothed_fps();

        if (m_active_scene && m_active_scene->get_cloth_system())
            m_active_scene->get_cloth_system()->set_frame_dt(ts.get_seconds());

        const bool should_render_scene = update_scene_for_current_state(ts);

        RenderCommand::clear();

        SceneViewportRenderSettings viewport_settings{};
        viewport_settings.renderer_type = Settings::get().renderer.renderer_type;
        viewport_settings.geometry_path = Settings::get().renderer.geometry_path;
        viewport_settings.collect_frame_graph_timings = m_collect_frame_graph_timings;
        viewport_settings.log_frame_graph_pass_timings = m_log_frame_graph_pass_timings;
        viewport_settings.debug_pick_enabled = false;
        m_scene_viewport_renderer.set_settings(viewport_settings);

        if (should_render_scene)
            m_scene_viewport_renderer.render(build_scene_viewport_render_context(ts));
    }

    void EditorLayer::on_imgui_render() {
        HN_PROFILE_FUNCTION();

        if (m_quit_requested) {
            if (has_scene_changed()) {
                m_notification_center.open_confirm("quit_editor",
                    "Quit Honey Editor?",
                    "You have unsaved changes. Are you sure you want to quit without saving?",
                    true,
                    []() { Application::quit(); },
                    [this]() { m_quit_requested = false; },
                    "Quit without saving",
                    "Cancel"
                );
            } else {
                Application::quit();
            }
            m_quit_requested = false;
        }

        ImGuiStyle& style = ImGui::GetStyle();
        float min_window_size = style.WindowMinSize.x;
        style.WindowMinSize.x = 370.0f;

        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        style.WindowMinSize.x = min_window_size;

        if (m_viewport_maximized) {
            draw_menu_bar();
            m_viewport_panel.on_imgui_render(*this);
            draw_ui_toolbar();
        } else {
            draw_menu_bar(); // TODO: Spend some time cleaning up how this is handled
            m_scene_hierarchy_panel.on_imgui_render();
            m_content_browser_panel.on_imgui_render();
            m_renderer_debug_panel.on_imgui_render(*this);
            m_physics_debug_panel.on_imgui_render();
            m_viewport_panel.on_imgui_render(*this);
            draw_ui_toolbar();
        }

        m_notification_center.render(ImGui::GetIO().DeltaTime);
    }

    void EditorLayer::draw_ui_toolbar() {
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
        const Ref<Texture2D> play_stop_icon = m_scene_state == SceneState::edit ? m_icon_play : m_icon_stop;
        const Ref<Texture2D> pause_icon = paused ? m_icon_play : m_icon_pause;
        bool in_play = (m_scene_state == SceneState::play || m_scene_state == SceneState::simulate);

        // Center all 3 buttons as a group
        ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x * 0.5f) - (size * 1.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

        // Play / Stop
        if (UI::ImageButton("play_stop_button", play_stop_icon->get_imgui_texture_id(), ImVec2(size, size))) {
            if (m_scene_state == SceneState::edit)
                on_scene_play();
            else
                on_scene_stop();
        }
        ImGui::SetItemTooltip(m_scene_state == SceneState::edit ? "Play" : "Stop");

        // Pause / Resume
        ImGui::SameLine();
        ImGui::BeginDisabled(!in_play);
        if (UI::ImageButton("pause_button", pause_icon->get_imgui_texture_id(), ImVec2(size, size)))
            paused = !paused;
        ImGui::EndDisabled();
        ImGui::SetItemTooltip(paused ? "Resume" : "Pause");

        // Simulate
        ImGui::SameLine();
        if (UI::ImageButton("simulate_button", m_icon_simulate->get_imgui_texture_id(), ImVec2(size, size))) {
            if (m_scene_state == SceneState::edit)
                on_scene_simulate();
            else if (m_scene_state == SceneState::simulate)
                on_scene_stop();
        }
        ImGui::SetItemTooltip(m_scene_state == SceneState::simulate ? "Stop Simulation" : "Simulate");

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);
        ImGui::End();
    }

    void EditorLayer::draw_menu_bar() {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New", "Ctrl+N")) {
                    if (has_scene_changed()) {
                        m_notification_center.open_confirm("new_scene",
                            "Create new scene?",
                            "This will discard all changes to the current scene and create a new one.",
                            true,
                            [this]() {
                                new_scene();
                                m_notification_center.push_toast(UI::ToastType::Success, "New Scene", "Created new scene and saved previous.");
                            },
                            nullptr,
                            "Discard and Continue",
                            "Cancel"
                        );
                    } else {
                        new_scene();
                        m_notification_center.push_toast(UI::ToastType::Success, "New Scene", "Created new scene.");
                    }
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
                    m_quit_requested = true;
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
                const char* min_max_viewport_label = m_viewport_maximized ? "Restore Viewport" : "Maximize Viewport";
                if (ImGui::MenuItem(min_max_viewport_label, "Shift+Space")) {
                    m_viewport_maximized = !m_viewport_maximized;
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
            if (ImGui::BeginMenu("Preferences")) {
                if (ImGui::MenuItem("Save settings to file")) {
                    Settings::save_to_file( asset_root / ".." / "config" / "settings.yaml" );
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Scripts")) {
                if (ImGui::MenuItem("Build Native Scripts")) {
                    ScriptLoader::get().unload_library();

                    int result = std::system("cmake --build . --target HoneyScripts");
                    if (result == 0) {
                        HN_CORE_INFO("Scripts built successfully, reloading...");
                        m_notification_center.push_toast(UI::ToastType::Success, "Scripts Built", "HoneyScripts built and reloaded successfully.");
                        //print current working directory
                        std::filesystem::path current_path = std::filesystem::current_path();
                        HN_CORE_INFO("Current working directory: {0}", current_path.string());
                        ScriptLoader::get().reload_library("assets/scripts/libHoneyScripts.so");
                    } else {
                        HN_CORE_ERROR("Script build failed with code: {0}", result);
                        m_notification_center.push_toast(UI::ToastType::Error, "Build Failed", "HoneyScripts build failed. See console for details.");
                    }
                }
                if (ImGui::MenuItem("Reload Native Scripts")) {
                    ScriptLoader::get().reload_library("assets/scripts/libHoneyScripts.so");
                    m_notification_center.push_toast(UI::ToastType::Info, "Scripts Reloaded", "HoneyScripts reloaded.");
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Build & Reload C# Scripts", "Ctrl+Shift+B")) {
                    bool ok = CSharpScriptEngine::build_and_reload();
                    if (ok)
                        m_notification_center.push_toast(UI::ToastType::Success, "C# Scripts Built", "Scripts compiled and reloaded.");
                    else
                        m_notification_center.push_toast(UI::ToastType::Error, "C# Build Failed", "dotnet build failed. See console for details.");
                }

                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Debug Shortcuts")) {
                if (ImGui::MenuItem("Dump Compiled Frame Graph")) {
                    if (m_scene_viewport_renderer.get_output_framebuffer()) {
                        m_scene_viewport_renderer.log_debug_dump("Editor Frame Graph");
                    } else {
                        HN_CORE_WARN("No compiled editor frame graph to dump.");
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void EditorLayer::on_event(Event &event) {
        EventDispatcher dispatcher(event);

        dispatcher.dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& e) {
                if (m_scene_state == SceneState::edit && m_viewport_hovered) {
                    m_editor_camera.on_event(e);
                    return true; // consume scroll so editor camera is the only receiver here
                }
                return false;
            });

        dispatcher.dispatch<KeyPressedEvent>(HN_BIND_EVENT_FN(EditorLayer::on_key_pressed));
        dispatcher.dispatch<MouseButtonPressedEvent>(HN_BIND_EVENT_FN(EditorLayer::on_mouse_button_pressed));
        dispatcher.dispatch<WindowCloseEvent>([this](WindowCloseEvent& e) {
            if (has_scene_changed()) {
                m_quit_requested = true;
                return true; // consume event
            }
            return false;
        });
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

        case KeyCode::Escape:
            if (m_scene_state == SceneState::play) {
                Application::get().get_window().set_cursor_captured(false);
                handled = true;
            }
            break;

        case KeyCode::Space:
            if (shift) {
                m_viewport_maximized = !m_viewport_maximized;
                handled = true;
            }
            break;

        case KeyCode::B:
            if (control && shift) {
                bool ok = CSharpScriptEngine::build_and_reload();
                if (ok)
                    m_notification_center.push_toast(UI::ToastType::Success, "C# Scripts Built", "Scripts compiled and reloaded.");
                else
                    m_notification_center.push_toast(UI::ToastType::Error, "C# Build Failed", "dotnet build failed. See console for details.");
                handled = true;
            }
            break;

            //scene file shortcuts
        case KeyCode::S:
            if (control && shift) { save_scene_as(); handled = true; }
            else if (control) { save_current_scene(); handled = true; }
            break;

        case KeyCode::O:
            if (control) { open_scene(); handled = true; }
            break;

        case KeyCode::N:
            if (control) {
                if (has_scene_changed()) {
                    m_notification_center.open_confirm("new_scene",
                            "Create new scene?",
                            "This will discard all changes to the current scene and create a new one.",
                            true,
                            [this]() {
                                new_scene();
                                m_notification_center.push_toast(UI::ToastType::Success, "New Scene", "Created new scene and saved previous.");
                            },
                            nullptr,
                            "Discard and Continue",
                            "Cancel"
                        );
                } else {
                    new_scene();
                    m_notification_center.push_toast(UI::ToastType::Success, "New Scene", "Created new scene.");
                }
                handled = true;
            }
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

        case KeyCode::Delete:
            {
                Entity selected = m_scene_hierarchy_panel.get_selected_entity();
                if (selected.is_valid()) {
                    m_scene_hierarchy_panel.delete_entity(selected);
                    handled = true;
                }
            }
             break;

        default:
            break;
        }

        return handled;
    }

    Entity EditorLayer::pick_entity_at_mouse() const {
        if (!m_active_scene)
            return Entity{};

        const float imgW = m_viewport_img_max.x - m_viewport_img_min.x;
        const float imgH = m_viewport_img_max.y - m_viewport_img_min.y;

        if (imgW <= 1.0f || imgH <= 1.0f)
            return Entity{};

        const ImVec2 mouse = ImGui::GetMousePos();
        const float localX = mouse.x - m_viewport_img_min.x;
        const float localY = mouse.y - m_viewport_img_min.y;

        if (localX < 0.0f || localY < 0.0f || localX >= imgW || localY >= imgH)
            return Entity{};

        const float sx = (imgW > 0.0f) ? (m_viewport_size.x / imgW) : 1.0f;
        const float sy = (imgH > 0.0f) ? (m_viewport_size.y / imgH) : 1.0f;

        const int mouse_x = (int)std::floor(localX * sx + 0.0001f);
        const int mouse_y = (int)std::floor((imgH - localY) * sy + 0.0001f);

        if (mouse_x < 0 || mouse_y < 0 ||
            mouse_x >= (int)m_viewport_size.x || mouse_y >= (int)m_viewport_size.y)
            return Entity{};

        return m_scene_viewport_renderer.pick_entity(
            static_cast<uint32_t>(mouse_x),
            static_cast<uint32_t>(mouse_y),
            m_active_scene.get());
    }

    bool EditorLayer::on_mouse_button_pressed(MouseButtonPressedEvent& e) {
        if (e.get_mouse_button() == MouseButton::Left) {

            if (m_scene_state == SceneState::play && m_viewport_hovered) {
                Application::get().get_window().set_cursor_captured(true);
                return false;
            }

            if (can_mousepick()) {
                // Click should be authoritative: pick right now.
                Entity clicked = pick_entity_at_mouse();
                m_scene_hierarchy_panel.set_selected_entity(clicked);
            }

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
            m_notification_center.push_toast(UI::ToastType::Success, "Scene Opened", "Loaded " + path.filename().string());

            // Restore editor camera if the scene had editor meta
            const EditorSceneMeta& meta = serializer.get_loaded_editor_meta();
            if (meta.has_camera) {
                // Reconstruct orbit state from yaw/pitch/fov/clip
                m_editor_camera.set_yaw(meta.camera_yaw);
                m_editor_camera.set_pitch(meta.camera_pitch);
                m_editor_camera.set_fov(meta.camera_fov);
                m_editor_camera.set_near_clip(meta.camera_near);
                m_editor_camera.set_far_clip(meta.camera_far);
            }
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

            // Serialize editor camera state so user can pick up exactly where they left off
            EditorSceneMeta meta{};
            meta.has_camera     = true;
            meta.camera_position = m_editor_camera.get_position();
            meta.camera_yaw     = m_editor_camera.get_yaw();
            meta.camera_pitch   = m_editor_camera.get_pitch();
            meta.camera_fov     = m_editor_camera.get_fov();
            meta.camera_near    = m_editor_camera.get_near_clip();
            meta.camera_far     = m_editor_camera.get_far_clip();

            SceneSerializer serializer(m_active_scene, &meta);
            serializer.serialize(path);
            m_editor_scene->clear_dirty();
            m_notification_center.push_toast(UI::ToastType::Success, "Scene Saved", "Scene saved to " + path.filename().string());
        }
    }

    void EditorLayer::save_current_scene() {
        save_scene_as(m_scene_filepath);
    }

    // Note to self, editor scene is null in some cases so the copy doesnt work, test from fresh scene on boot, ctrlN scene, and saved scene to confirm

    bool EditorLayer::has_scene_changed() {
       return m_editor_scene->get_change_version() != 0;
    }

    void EditorLayer::on_scene_play() {
        m_scene_state = SceneState::play;

        m_active_scene = Scene::copy(m_editor_scene);
        m_active_scene->on_viewport_resize((uint32_t)m_viewport_size.x, (uint32_t)m_viewport_size.y);
        m_active_scene->on_runtime_start();
        m_scene_hierarchy_panel.set_context(m_active_scene);
    }

    void EditorLayer::on_scene_simulate() {
        m_scene_state = SceneState::simulate;

        m_active_scene = Scene::copy(m_editor_scene);
        m_active_scene->on_viewport_resize((uint32_t)m_viewport_size.x, (uint32_t)m_viewport_size.y);
        m_active_scene->on_runtime_start();
        m_scene_hierarchy_panel.set_context(m_active_scene);
    }

    void EditorLayer::on_scene_stop() {
        m_scene_state = SceneState::edit;
        paused = false;

        Application::get().get_window().set_cursor_captured(false);
        m_active_scene->on_runtime_stop();
        m_active_scene = m_editor_scene;
        m_scene_hierarchy_panel.set_context(m_active_scene);
    }

    void EditorLayer::on_duplicate_entity() {
        if (m_scene_state != SceneState::edit) return;

        Entity entity = m_scene_hierarchy_panel.get_selected_entity();
        if (entity) {
            m_editor_scene->duplicate_entity(entity);
            m_notification_center.push_toast(UI::ToastType::Info, "Entity Duplicated", "Duplicated " + entity.get_component<TagComponent>().tag);
        }
    }
}
