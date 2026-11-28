#pragma once
#include <Honey.h>
#include "panels/scene_hierarchy_panel.h"
#include "panels/content_browser_panel.h"
#include "Honey/renderer/scene_viewport_renderer.h"
#include "Honey/loaders/gltf_loader.h"

#include <glm/glm/glm.hpp>
#include <imgui.h>
#include <ImGuizmo.h>

#include "Honey/ui/notifications.h"
#include "panels/physics_debug_panel.h"
#include "panels/renderer_debug_panel.h"
#include "panels/viewport_panel.h"
#include "panels/console_panel.h"
#include <vector>
#include <functional>
#include "Honey/core/asset_paths.h"


namespace Honey {
    class EditorLayer : public Layer {

    public:
        EditorLayer();
        virtual ~EditorLayer() = default;

        virtual void on_attach() override;
        virtual void on_detach() override;

        void on_update(Timestep ts) override;
        virtual void on_imgui_render() override;
        void on_event(Event &event) override;

    private:

        bool on_key_pressed(KeyPressedEvent& e);
        bool on_mouse_button_pressed(MouseButtonPressedEvent& e);

        void on_overlay_render();

        bool can_mousepick() { return (m_viewport_hovered && !ImGuizmo::IsOver() && !Input::is_key_pressed(KeyCode::LeftAlt)); }
        Entity pick_entity_at_mouse() const;

        // --- Projects (phase 1.5 step 3) ---
        // All of these run on the main thread between frames of work; each stops play mode first.
        void open_project(const std::filesystem::path& project);   // folder or .honeyproj
        void open_project_dialog();
        void close_project();
        void request_project_switch(std::function<void()> action);  // confirms if the scene has unsaved changes
        bool load_project(const std::filesystem::path& project);   // switch; false (state unchanged) on failure
        void enter_project();                                      // AssetPaths + manifest already point at the project
        void update_window_title();
        void draw_welcome_window();
        void draw_new_project_popup();
        bool has_project() const { return AssetPaths::has_project(); }

        void new_scene();
        void open_scene();
        void open_scene(const std::filesystem::path& path);
        void save_scene_as(std::filesystem::path path = "");
        void save_current_scene();
        bool has_scene_changed();

        void on_scene_play();
        void on_scene_simulate();
        void on_scene_stop();
        void on_duplicate_entity();
        bool update_scene_for_current_state(Timestep ts);
        SceneViewportRenderContext build_scene_viewport_render_context(Timestep ts);

        // ui panels
        void draw_ui_toolbar();
        void draw_menu_bar();

        SceneViewportRenderer m_scene_viewport_renderer;
        bool m_collect_frame_graph_timings = true;
        bool m_log_frame_graph_pass_timings = false;
        glm::vec2 m_viewport_size = {1680.0f, 720.0f};
        bool m_viewport_focused = false, m_viewport_hovered = false;
        bool m_viewport_visible = false; // Viewport window is on screen this frame (not collapsed / hidden behind another dock tab)
        bool m_viewport_maximized = false;

        bool      m_viewport_resize_pending = false;
        glm::vec2 m_pending_viewport_size   = {0.0f, 0.0f};

        std::filesystem::path m_scene_filepath;
        Ref<Scene> m_active_scene;
        Ref<Scene> m_editor_scene;

        ImVec2 m_viewport_img_min = { 0.0f, 0.0f };
        ImVec2 m_viewport_img_max = { 0.0f, 0.0f };

        //Editor resources
        EditorCamera m_editor_camera;
        Ref<Texture2D> m_icon_play, m_icon_stop, m_icon_pause, m_icon_simulate;
        Ref<VectorIcon> m_icon_point_light, m_icon_dir_light, m_icon_camera;

        UI::NotificationCenter m_notification_center;
        bool m_quit_requested = false;

        // New Project popup state
        bool m_new_project_open = false;
        char m_new_project_name[64] = "";
        char m_new_project_location[512] = "";
        std::string m_new_project_error;

        std::function<void()> m_deferred_action;   // run at the top of the next on_update, before any rendering

        std::string m_project_name;   // from the .honeyproj; empty when no project is open

        FramerateCounter m_framerate_counter;
        int m_framerate = 0;
        float m_frame_time = 0.0f;

        int m_gizmo_type = -1;
        bool m_show_physics_colliders = false;
        bool m_show_notification_history = false;

        //Panels
        SceneHierarchyPanel m_scene_hierarchy_panel;
        PropertiesPanel     m_properties_panel{m_scene_hierarchy_panel};
        ContentBrowserPanel m_content_browser_panel;
        PhysicsDebugPanel   m_physics_debug_panel;
        ConsolePanel        m_console_panel;

        // Panels derived from EditorPanel: rendered while open, toggled from the View menu.
        std::vector<EditorPanel*> m_panels;
        friend class RendererDebugPanel;
        RendererDebugPanel  m_renderer_debug_panel;
        friend class ViewportPanel;
        ViewportPanel       m_viewport_panel;

        enum class SceneState {

            edit = 0,
            play = 1,
            pause = 2,
            stop = 3,
            simulate = 4
        };

        SceneState m_scene_state = SceneState::edit;
        bool paused = false;

        struct PendingGltfLoad {
            Ref<GltfSceneTreeAsyncHandle> handle;
            std::filesystem::path source_path;
        };
        std::vector<PendingGltfLoad> m_pending_gltf_loads;

    };
}
