#include "hnpch.h"
#include "renderer_debug_panel.h"
#include "Honey/core/settings.h"

#include <imgui.h>

#include "../editor_layer.h"
#include "Honey/core/engine.h"
#include "Honey/renderer/renderer_2d.h"
#include "Honey/renderer/render_command.h"
#include "Honey/renderer/texture_cache.h"
#include "Honey/scene/entity.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {

    namespace {

    }

    RendererDebugPanel::RendererDebugPanel() {}

    void RendererDebugPanel::on_imgui_render(EditorLayer& editor) {
        ImGui::Begin("Renderer Debug Panel");

        render_camera_info(editor);

        if (ImGui:: CollapsingHeader("Renderer Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Current API: %s", RendererAPI::to_string(RendererAPI::get_api()));

            static std::string vendor_cache;
            if (vendor_cache.empty()) {
                vendor_cache = RenderCommand::get_renderer_api()->get_vendor();
            }
            ImGui::Text("Vendor: %s", vendor_cache.c_str());
        }

        if (ImGui::CollapsingHeader("Cloth Sim", ImGuiTreeNodeFlags_DefaultOpen)) {
            const bool has_system = editor.m_active_scene && editor.m_active_scene->get_cloth_system();
            ImGui::Text("System: %s", has_system ? "Active" : "None");
            ImGui::Text("(Add a ClothComponent to an entity and press Play)");
        }

        // Performance Section
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Frame Rate: %d FPS", editor.m_framerate);
            ImGui::Text("Frame Time: %.3f ms", editor.m_frame_time);

            double gpu_ms = 0.0;
            {
                auto& app = Application::get();
                auto& window = app.get_window();
                auto* ctx = window.get_context();
                gpu_ms = ctx->get_last_gpu_frame_time_ms();
                ImGui::Text("GPU Frame Time: %.3f ms", gpu_ms);
                ImGui::Text("GPU time spent ratio: %.3f %%", (gpu_ms / editor.m_frame_time) * 100.0f);

                const uint32_t zone_count = ctx->get_gpu_zone_count();
                if (zone_count > 0) {
                    ImGui::Indent();
                    for (uint32_t i = 0; i < zone_count; ++i) {
                        const char*  name = ctx->get_gpu_zone_name(i);
                        const double ms   = ctx->get_gpu_zone_time_ms(i);
                        ImGui::Text("%-20s %.3f ms", name, ms);
                    }
                    ImGui::Unindent();
                }
            }
            //ImGui::Text("Smoothed FPS: %d", editor.m_framerate_counter.get_smoothed_fps());

            ImGui::Separator();
            auto stats = Renderer2D::get_stats();
            ImGui::Text("Draw Calls: %d", stats.draw_calls);
            ImGui::Text("Quads: %d", stats.quad_count);
            ImGui::Text("Vertices: %d", stats.get_total_vertex_count());
            ImGui::Text("Indices: %d", stats.get_total_index_count());

            if (ImGui::Button("Reset Statistics")) {
                Renderer2D::reset_stats();
            }

            ImGui::Separator();
            const auto& fg_stats = editor.m_scene_viewport_renderer.get_frame_graph_stats();
            ImGui::Text("Frame Graph CPU: %.3f ms", fg_stats.total_cpu_time_ms);
            ImGui::Checkbox("Collect FG CPU Timings", &editor.m_collect_frame_graph_timings);
            ImGui::Checkbox("Log FG Pass Timings", &editor.m_log_frame_graph_pass_timings);

            if (ImGui::TreeNode("Frame Graph Pass Timings")) {
                if (fg_stats.pass_stats.empty()) {
                    ImGui::TextDisabled("No pass timing data yet.");
                } else {
                    for (const auto& pass_stat : fg_stats.pass_stats) {
                        ImGui::Text("%s: %.3f ms%s",
                                    pass_stat.pass_name.c_str(),
                                    pass_stat.cpu_time_ms,
                                    pass_stat.skipped ? " (skipped)" : "");
                    }
                }
                ImGui::TreePop();
            }
        }

        // Renderer Settings Section
        if (ImGui::CollapsingHeader("Renderer Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& renderer = Settings::get().renderer;

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
                    renderer.texture_filter = static_cast<RendererSettings::TextureFilter>(current);

                    TextureCache::get().recreate_all_samplers();
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

            // Cull mode
            {
                static const char* cull_mode_names[] = { "None", "Back", "Front" };
                int current_index = static_cast<int>(renderer.cull_mode);
                if (ImGui::Combo("Cull Mode", &current_index, cull_mode_names, IM_ARRAYSIZE(cull_mode_names))) {
                    renderer.cull_mode = static_cast<CullMode>(current_index);
                    RenderCommand::set_cull_mode(renderer.cull_mode);
                }
            }

            // Geometry path
            {
                static const char* geometry_path_names[] = { "Classic", "Meshlet" };
                int current_index = static_cast<int>(renderer.geometry_path);
                if (ImGui::Combo("Geometry Path", &current_index, geometry_path_names, IM_ARRAYSIZE(geometry_path_names))) {
                    renderer.geometry_path = static_cast<GeometryPath>(current_index);
                    Renderer3D::set_geometry_render_path(renderer.geometry_path);
                }
            }

            // Renderer type
            {
                static const char* renderer_type_names[] = { "Forward", "Deferred", "Path Tracer" };
                int current_index = static_cast<int>(renderer.renderer_type);
                if (ImGui::Combo("Renderer Type", &current_index, renderer_type_names, IM_ARRAYSIZE(renderer_type_names))) {
                    renderer.renderer_type = static_cast<RendererSettings::RendererType>(current_index);
                    editor.m_scene_viewport_renderer.mark_frame_graph_dirty();
                }
            }

            if (ImGui::Checkbox("Show Physics Colliders", &renderer.show_physics_debug_draw)) {
                editor.m_show_physics_colliders = renderer.show_physics_debug_draw;
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("V-Sync", &renderer.vsync)) {
                RenderCommand::set_vsync(renderer.vsync);
            }

            const char* api_names[] = { "OpenGL", "Vulkan" };
            int api_index = (static_cast<int>(renderer.api) - 1);
            if (ImGui::Combo("Renderer API", &api_index, api_names, IM_ARRAYSIZE(api_names))) {
                renderer.api = static_cast<RendererAPI::API>(++api_index);
                // Show small tooltip or text: "Takes effect on next restart"
                Settings::write_renderer_api_to_file( asset_root / ".." / "config" / "settings.yaml" );
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Renderer API changes will take effect after restarting the editor.");
            }

            if (ImGui::CollapsingHeader("Viewport", ImGuiTreeNodeFlags_DefaultOpen)) {
                static const char* modes[] = { "Color", "Picking View" };
                int mode = static_cast<int>(m_viewport_display_mode);
                if (ImGui::Combo("Display Mode", &mode, modes, IM_ARRAYSIZE(modes))) {
                    m_viewport_display_mode = static_cast<ViewportDisplayMode>(mode);
                }
            }

            if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat("Directional shadow draw distance", &renderer.dir_shadow_distance, 5.0f, 0.0f, 1000.0f);

            }

        }

        ImGui::End();
    }

    void RendererDebugPanel::render_camera_info(EditorLayer& editor) {
        if (!ImGui::CollapsingHeader("Camera Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }

        std::vector<Entity> camera_entities;
        if (editor.m_active_scene) {
            auto camera_view = editor.m_active_scene->get_registry().view<CameraComponent>();
            for (auto entity_id : camera_view) {
                Entity camera_entity = { entity_id, editor.m_active_scene.get() };
                if (camera_entity.is_valid()) {
                    camera_entities.push_back(camera_entity);
                }
            }
        }

        Entity current_primary_camera = editor.m_active_scene->get_primary_camera();


        std::string current_primary = "None";
        if (current_primary_camera.is_valid()) {
            current_primary = current_primary_camera.get_component<TagComponent>().tag;
        }

        ImGui::Text("Current Primary: %s", current_primary.c_str());
        ImGui::Text("Total Cameras: %zu", camera_entities.size());
    }
}
