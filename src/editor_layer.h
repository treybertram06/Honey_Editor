#pragma once
#include <Honey.h>
#include "panels/scene_hierarchy_panel.h"
#include "panels/content_browser_panel.h"

#include <glm/glm/glm.hpp>
#include <imgui.h>
#include <ImGuizmo.h>


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

        bool can_mousepick() { return (m_viewport_hovered && !ImGuizmo::IsOver() && !Input::is_key_pressed(KeyCode::LeftAlt)); }

        void new_scene();
        void open_scene();
        void open_scene(const std::filesystem::path& path);
        void save_scene_as();

        void on_scene_play();
        void on_scene_stop();

        // ui panels
        void ui_toolbar();



        Ref<Framebuffer> m_framebuffer;
        glm::vec2 m_viewport_size = {1680.0f, 720.0f};
        bool m_viewport_focused = false, m_viewport_hovered = false;

        Ref<Scene> m_active_scene;
        Entity m_camera_ent;
        Entity m_square_ent;
        std::vector<Entity> m_camera_entities;
        Entity m_hovered_entity;

        //Editor camera
        EditorCamera m_editor_camera;

        //Editor resources
        Ref<Texture2D> m_icon_play, m_icon_stop;



        glm::vec4 m_clear_color = { 0.1f, 0.1f, 0.1f, 1.0f };

        FramerateCounter m_framerate_counter;
        int m_framerate = 0;
        float m_frame_time = 0.0f;


        int m_gizmo_type = -1;

        //Panels
        SceneHierarchyPanel m_scene_hierarchy_panel;
        ContentBrowserPanel m_content_browser_panel;

        void render_camera_selector();

        enum class SceneState {
            edit = 0,
            play = 1,
            pause = 2,
            stop = 3,
        };

        SceneState m_scene_state = SceneState::edit;


    };
}
