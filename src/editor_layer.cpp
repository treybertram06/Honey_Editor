#include "editor_layer.h"

#include "imgui.h"
#include <glm/glm/gtc/type_ptr.hpp>
#include "hnpch.h"
#include "../assets/scripts/camera_controller.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {

    EditorLayer::EditorLayer()
        : Layer("EditorLayer"),
          m_camera_controller((1.6f / 0.9f), true)
    {}


    void EditorLayer::on_attach() {

        FramebufferSpecification fb_spec;
        fb_spec.width = 1280;
        fb_spec.height = 720;
        m_framebuffer = Framebuffer::create(fb_spec);

        m_active_scene = CreateRef<Scene>();

        m_square_ent = m_active_scene->create_entity("Square");
        auto second_square = m_active_scene->create_entity("Second Square");

        m_camera_ent = m_active_scene->create_entity("Main Camera");
        auto second_camera = m_active_scene->create_entity("Secondary Camera");
        auto third_camera = m_active_scene->create_entity("Debug Camera");

        m_square_ent.add_component<SpriteRendererComponent>(glm::vec4(0.8f, 0.3f, 0.8f, 1.0f));
        second_square.add_component<SpriteRendererComponent>(glm::vec4(0.2f, 0.3f, 0.8f, 1.0f));
        second_square.get_component<TransformComponent>().translation = glm::vec3(0.5f, 2.0f, 0.0f);

        m_camera_ent.add_component<CameraComponent>();
        second_camera.add_component<CameraComponent>();
        third_camera.add_component<CameraComponent>();

        // Store camera entities for the radio buttons
        m_camera_entities = { m_camera_ent, second_camera, third_camera };

        m_active_scene->set_primary_camera(m_camera_ent);



        m_camera_ent.add_component<NativeScriptComponent>().bind<CameraController>();
        //second_camera.add_component<NativeScriptComponent>().bind<CameraController>();
        //third_camera.add_component<NativeScriptComponent>().bind<CameraController>();


        m_scene_hierarchy_panel.set_context(m_active_scene);


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


        if (m_viewport_focused)
            m_camera_controller.on_update(ts);



        m_framerate_counter.update(ts);
        m_framerate = m_framerate_counter.get_smoothed_fps();



        m_framebuffer->bind();
        RenderCommand::set_clear_color(m_clear_color);
        RenderCommand::clear();




        m_active_scene->on_update(ts);
        m_active_scene->render();

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
                if (ImGui::MenuItem("New")) {
                    // Action for New
                }
                if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                    // Action for Open
                }
                if (ImGui::MenuItem("Save", "Ctrl+S")) {
                    // Action for Save
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

                    for (int i = 0; i < 6; ++i) {
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

        /* Texture Manager Section
        if (ImGui::CollapsingHeader("Texture Manager")) {
            ImGui::Text("Loaded Textures:");
            ImGui::BulletText("Chuck Texture: %s", m_chuck_texture ? "Loaded" : "Not Loaded");
            ImGui::BulletText("Missing Texture: %s", m_missing_texture ? "Loaded" : "Not Loaded");
            ImGui::BulletText("Transparent Texture: %s", m_sprite_sheet01 ? "Loaded" : "Not Loaded");

            if (ImGui::Button("Reload Textures")) {
                on_attach(); // Quick way to reload textures
            }
        }
*/
        ImGui::End();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport");

        m_viewport_focused = ImGui::IsWindowFocused();
        m_viewport_hovered = ImGui::IsWindowHovered();
        Application::get().get_imgui_layer()->block_events(!m_viewport_focused || !m_viewport_hovered);

        ImVec2 viewport_panel_size = ImGui::GetContentRegionAvail();
        if (m_viewport_size != *((glm::vec2*)&viewport_panel_size)) {
            m_viewport_size = {viewport_panel_size.x, viewport_panel_size.y};
            m_framebuffer->resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);

            m_active_scene->on_viewport_resize((std::uint32_t)m_viewport_size.x, (std::uint32_t)m_viewport_size.y);

        }

        std::uint32_t texture_id = m_framebuffer->get_color_attachment_renderer_id();
        ImGui::Image(ImTextureID((void*)(intptr_t)texture_id), ImVec2(m_viewport_size.x, m_viewport_size.y), ImVec2(0,1), ImVec2(1,0));

        ImGui::End();
        ImGui::PopStyleVar();

        if (m_viewport_hovered)
            ImGui::GetIO().WantCaptureMouse = false;






    }

    void EditorLayer::on_event(Event &event) {
        m_camera_controller.on_event(event);
    }
}
