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

        Honey::FramebufferSpecification fb_spec;
        fb_spec.width = 1280;
        fb_spec.height = 720;
        m_framebuffer = Honey::Framebuffer::create(fb_spec);

        auto texture_path_prefix = asset_root / "textures";
        m_chuck_texture = Honey::Texture2D::create(texture_path_prefix / "bung.png");
        m_missing_texture = Honey::Texture2D::create(texture_path_prefix / "missing.png");
        m_sprite_sheet01 = Honey::Texture2D::create(asset_root / "test_game" / "textures"/ "roguelikeSheet_transparent.png");
        m_sprite_sheet02 = Honey::Texture2D::create(asset_root / "test_game" / "textures"/ "colored-transparent.png");
        m_bush_sprite = Honey::SubTexture2D::create_from_coords(m_sprite_sheet01, {14, 9},{16, 16},{1, 1},{1, 1},{0, 17});
        s_texture_map['d'] = Honey::SubTexture2D::create_from_coords(m_sprite_sheet01, {5, 0},{16, 16},{1, 1},{1, 1},{0, 17});
        s_texture_map['w'] = Honey::SubTexture2D::create_from_coords(m_sprite_sheet01, {3, 1},{16, 16},{1, 1},{1, 1},{0, 17});
        m_player_sprite = Honey::SubTexture2D::create_from_coords(m_sprite_sheet02, {23, 7},{16, 16},{1, 1},{1, 1},{0, 17});


        m_camera_controller.set_zoom_level(10.0f);
    }

    void EditorLayer::on_detach() {
    }

    void EditorLayer::on_update(Honey::Timestep ts) {
        HN_PROFILE_FUNCTION();

        m_frame_time = ts.get_millis();

        // update
        {
            HN_PROFILE_SCOPE("EditorLayer::camera_update");
            m_camera_controller.on_update(ts);
        }

        //profiling
        {
            HN_PROFILE_SCOPE("EditorLayer::framerate");
            m_framerate_counter.update(ts);
            m_framerate = m_framerate_counter.get_smoothed_fps();
        }

        {
            HN_PROFILE_SCOPE("EditorLayer::renderer_clear");
            // render
            m_framebuffer->bind();
            Honey::RenderCommand::set_clear_color(m_clear_color);
            Honey::RenderCommand::clear();
        }

        {
            HN_PROFILE_SCOPE("EditorLayer::renderer_draw");
            Honey::Renderer2D::begin_scene(m_camera_controller.get_camera());

            static float rotation = 0.0f;
            rotation += glm::radians(15.0f) * ts;


            Honey::Renderer2D::draw_quad({0.0f, 0.0f}, {1.0f, 1.0f}, {0.2f, 0.2f, 0.8f, 1.0f});
            Honey::Renderer2D::draw_quad({2.0f, 2.0f}, {1.0f, 1.0f}, {0.8f, 0.2f, 0.3f, 1.0f});
            Honey::Renderer2D::draw_quad({0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}, m_chuck_texture, {1.0f, 0.5f, 0.5f, 1.0f}, 1.0f);
            Honey::Renderer2D::draw_quad({0.0f, -1.0f, 0.0f}, {1.5f, 1.5f}, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);

            //Honey::Renderer2D::draw_rotated_quad({0.5f, 1.5f, 0.0f}, {3.0f, 3.0f}, rotation, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
            //Honey::Renderer2D::draw_rotated_quad({0.5f, -1.5f, 0.0f}, {3.0f, 3.0f}, 0.0f, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
            Honey::Renderer2D::draw_quad({0.0f, 0.0f, -0.1f}, {100.0f, 100.0f}, m_missing_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 1000.0f);


            /*
                    Honey::Renderer2D::begin_scene(m_camera_controller.get_camera());
            
                    //Honey::Renderer2D::draw_quad({0.0f, 0.0f, 0.0f}, {96.8f, 52.6f}, m_sprite_sheet, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
                    for (uint32_t y = 0; y < m_map_height; y++) {
                        for (uint32_t x = 0; x < m_map_width; x++) {
                            char tile_type = s_map_tiles[x + y * m_map_width];
                            if (s_texture_map.find(tile_type) != s_texture_map.end()) {
                                auto texture = s_texture_map[tile_type];
                                Honey::Renderer2D::draw_quad({(float)x - (m_map_width / 2), (float)y - (m_map_height / 2), 0.0f}, {1.0f, 1.0f}, texture, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
                            } else {
                                auto texture = m_missing_texture;
                                Honey::Renderer2D::draw_quad({(float)x - (m_map_width / 2), (float)y - (m_map_height / 2), 0.0f}, {1.0f, 1.0f}, texture, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
                            }
                        }
                    }
                    */

            //Honey::Renderer2D::draw_quad({-0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}, m_bush_sprite, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
            //Honey::Renderer2D::draw_quad({1.5f, 1.5f, 0.0f}, {1.0f, 1.0f}, m_water_sprite, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);



            /*
            for (int x = 0; x < 1000; x++) {
                for (int y = 0; y < 1000; y++) {
                    Honey::Renderer2D::draw_quad({x*0.11f, y*0.11f, 0.0f}, {0.1f, 0.1f}, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
                }
            }
            */

            Honey::Renderer2D::end_scene();
            m_framebuffer->unbind();
        }

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
                    Honey::Application::quit();
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
            auto stats = Honey::Renderer2D::get_stats();
            ImGui::Text("Draw Calls: %d", stats.draw_calls);
            ImGui::Text("Quads: %d", stats.quad_count);
            ImGui::Text("Vertices: %d", stats.get_total_vertex_count());
            ImGui::Text("Indices: %d", stats.get_total_index_count());

            if (ImGui::Button("Reset Statistics")) {
                Honey::Renderer2D::reset_stats();
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
                Honey::RenderCommand::set_wireframe(wireframe_mode);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Depth Test", &depth_test)) {
                Honey::RenderCommand::set_depth_test(depth_test);
            }

            if (ImGui::Checkbox("Face Culling", &face_culling)) {
                // Honey::RenderCommand::set_face_culling(face_culling);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Blending", &blending)) {
                Honey::RenderCommand::set_blend(blending);
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

        // Object Properties Section
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
        }

        // Lighting Section
        if (ImGui::CollapsingHeader("Lighting")) {
            static glm::vec3 light_direction = {-0.2f, -1.0f, -0.3f};
            static glm::vec3 light_color = {1.0f, 1.0f, 1.0f};
            static float light_intensity = 1.0f;
            static glm::vec3 ambient_color = {0.2f, 0.2f, 0.2f};

            ImGui::DragFloat3("Light Direction", glm::value_ptr(light_direction), 0.1f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Light Color", glm::value_ptr(light_color));
            ImGui::SliderFloat("Light Intensity", &light_intensity, 0.0f, 5.0f);
            ImGui::ColorEdit3("Ambient Color", glm::value_ptr(ambient_color));
        }

        // Post-Processing Section
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
        }

        // Debug Section
        if (ImGui::CollapsingHeader("Debug")) {
            static bool show_wireframe = false;
            static bool show_normals = false;
            static bool show_bounding_boxes = false;
            static bool show_grid = false;

            if (ImGui::Checkbox("Show Wireframe", &show_wireframe)) {
                Honey::RenderCommand::set_wireframe(show_wireframe);
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

        // Texture Manager Section
        if (ImGui::CollapsingHeader("Texture Manager")) {
            ImGui::Text("Loaded Textures:");
            ImGui::BulletText("Chuck Texture: %s", m_chuck_texture ? "Loaded" : "Not Loaded");
            ImGui::BulletText("Missing Texture: %s", m_missing_texture ? "Loaded" : "Not Loaded");
            ImGui::BulletText("Transparent Texture: %s", m_sprite_sheet01 ? "Loaded" : "Not Loaded");

            if (ImGui::Button("Reload Textures")) {
                on_attach(); // Quick way to reload textures
            }
        }

        ImGui::End();

        ImGui::Begin("Viewport");

        uint32_t texture_id = m_framebuffer->get_color_attachment_renderer_id();
        ImVec2 size = ImVec2(1280.0f, 720.0f);
        ImGui::Image(ImTextureID((void*)(intptr_t)texture_id), size, ImVec2(0,1), ImVec2(1,0));

        ImGui::End();




    }

    void EditorLayer::on_event(Honey::Event &event) {
        m_camera_controller.on_event(event);
    }
}
