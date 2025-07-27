#include "editor_layer.h"

#include "imgui.h"
#include <glm/glm/gtc/type_ptr.hpp>
#include "hnpch.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {

    EditorLayer::EditorLayer()
        : Layer("EditorLayer"),
          m_camera_controller((1.6f / 0.9f), true) {
    }


    void EditorLayer::on_attach() {

        FramebufferSpecification fb_spec;
        fb_spec.width = 1280;
        fb_spec.height = 720;
        m_framebuffer = Framebuffer::create(fb_spec);

        m_active_scene = CreateRef<Scene>();

        auto chuck = m_active_scene->create_entity();
        m_active_scene->reg().emplace<TransformComponent>(chuck);
        m_active_scene->reg().emplace<SpriteRendererComponent>(chuck, glm::vec4{0.0f, 1.0f, 0.0f, 1.0f});

        auto texture_path_prefix = asset_root / "textures";
        m_chuck_texture = Texture2D::create(texture_path_prefix / "bung.png");
        m_missing_texture = Texture2D::create(texture_path_prefix / "missing.png");
        m_sprite_sheet01 = Texture2D::create(asset_root / "test_game" / "textures"/ "roguelikeSheet_transparent.png");
        m_sprite_sheet02 = Texture2D::create(asset_root / "test_game" / "textures"/ "colored-transparent.png");
        m_bush_sprite = SubTexture2D::create_from_coords(m_sprite_sheet01, {14, 9},{16, 16},{1, 1},{1, 1},{0, 17});
        s_texture_map['d'] = SubTexture2D::create_from_coords(m_sprite_sheet01, {5, 0},{16, 16},{1, 1},{1, 1},{0, 17});
        s_texture_map['w'] = SubTexture2D::create_from_coords(m_sprite_sheet01, {3, 1},{16, 16},{1, 1},{1, 1},{0, 17});
        m_player_sprite = SubTexture2D::create_from_coords(m_sprite_sheet02, {23, 7},{16, 16},{1, 1},{1, 1},{0, 17});


        m_camera_controller.set_zoom_level(10.0f);
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




        Renderer2D::begin_scene(m_camera_controller.get_camera());

        m_active_scene->on_update(ts);

        Renderer2D::end_scene();
        m_framebuffer->unbind();
    }

    void EditorLayer::on_imgui_render() {
        HN_PROFILE_FUNCTION();

        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

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
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }


        ImGui::Begin("Renderer Debug Panel");

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
            static bool depth_test = true;
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

        // Camera Control Section
        if (ImGui::CollapsingHeader("Camera Control", ImGuiTreeNodeFlags_DefaultOpen)) {
            glm::vec3 camera_pos = m_camera_controller.get_camera().get_position();
            if (ImGui::DragFloat3("Position", glm::value_ptr(camera_pos), 0.1f)) {
                m_camera_controller.get_camera().set_position(camera_pos);
            }

            float camera_rotation = m_camera_controller.get_camera().get_rotation();
            if (ImGui::SliderFloat("Rotation", &camera_rotation, -180.0f, 180.0f)) {
                m_camera_controller.get_camera().set_rotation(camera_rotation);
            }

            float zoom = m_camera_controller.get_zoom_level();
            if (ImGui::SliderFloat("Zoom Level", &zoom, 0.1f, 10.0f)) {
                m_camera_controller.set_zoom_level(zoom);
            }

            ImGui::Separator();
            ImGui::Text("Projection Settings");

            if (ImGui::Button("Reset Camera")) {
                m_camera_controller.get_camera().set_position({0.0f, 0.0f, 0.0f});
                m_camera_controller.get_camera().set_rotation(0.0f);
                m_camera_controller.set_zoom_level(1.0f);
            }
        }

        /* Object Properties Section
        if (ImGui::CollapsingHeader("Object Properties")) {
            ImGui::Text("Texture Settings");

            static float texture_tiling = 1.0f;
            ImGui::SliderFloat("Texture Tiling", &texture_tiling, 0.1f, 10.0f);

            static glm::vec4 texture_color = {1.0f, 1.0f, 1.0f, 1.0f};
            ImGui::ColorEdit4("Texture Tint", glm::value_ptr(texture_color));

            ImGui::Separator();
            ImGui::Text("Test Object Transforms");

            static glm::vec3 test_position = {0.0f, 0.0f, 0.0f};
            static glm::vec2 test_scale = {1.0f, 1.0f};
            static float test_rotation = 0.0f;

            ImGui::DragFloat3("Test Position", glm::value_ptr(test_position), 0.1f);
            ImGui::DragFloat2("Test Scale", glm::value_ptr(test_scale), 0.1f, 0.1f, 10.0f);
            ImGui::SliderFloat("Test Rotation", &test_rotation, -180.0f, 180.0f);
        }*/

        /* Lighting Section
        if (ImGui::CollapsingHeader("Lighting")) {
            static glm::vec3 light_direction = {-0.2f, -1.0f, -0.3f};
            static glm::vec3 light_color = {1.0f, 1.0f, 1.0f};
            static float light_intensity = 1.0f;
            static glm::vec3 ambient_color = {0.2f, 0.2f, 0.2f};

            ImGui::DragFloat3("Light Direction", glm::value_ptr(light_direction), 0.1f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Light Color", glm::value_ptr(light_color));
            ImGui::SliderFloat("Light Intensity", &light_intensity, 0.0f, 5.0f);
            ImGui::ColorEdit3("Ambient Color", glm::value_ptr(ambient_color));
        }*/

        /* Post-Processing Section
        if (ImGui::CollapsingHeader("Post-Processing")) {
            static float gamma = 2.2f;
            static float exposure = 1.0f;
            static float contrast = 1.0f;
            static float brightness = 0.0f;
            static float saturation = 1.0f;

            ImGui::SliderFloat("Gamma", &gamma, 0.1f, 5.0f);
            ImGui::SliderFloat("Exposure", &exposure, 0.1f, 5.0f);
            ImGui::SliderFloat("Contrast", &contrast, 0.0f, 3.0f);
            ImGui::SliderFloat("Brightness", &brightness, -1.0f, 1.0f);
            ImGui::SliderFloat("Saturation", &saturation, 0.0f, 3.0f);
        }*/

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
