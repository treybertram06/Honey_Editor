#pragma once
#include <Honey.h>
#include "panels/scene_hierarchy_panel.h"
#include <glm/glm/glm.hpp>

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
        OrthographicCameraController m_camera_controller;

        // temp
        Ref<VertexArray> m_square_vertex_array;
        Ref<Shader> m_shader;
        Ref<Texture2D> m_missing_texture;
        Ref<Texture2D> m_chuck_texture, m_sprite_sheet01, m_sprite_sheet02;
        Ref<SubTexture2D> m_bush_sprite, m_grass_sprite, m_player_sprite, m_water_sprite;

        Ref<Framebuffer> m_framebuffer;
        glm::vec2 m_viewport_size = {1680.0f, 720.0f};
        bool m_viewport_focused = false, m_viewport_hovered = false;

        Ref<Scene> m_active_scene;
        Entity m_camera_ent;
        Entity m_square_ent;
        std::vector<Entity> m_camera_entities;



        std::uint32_t m_map_width, m_map_height;

        glm::vec4 m_clear_color = { 0.1f, 0.1f, 0.1f, 1.0f };
        glm::vec3 m_square_position;

        std::unordered_map<char, Ref<SubTexture2D>> s_texture_map;

        FramerateCounter m_framerate_counter;
        int m_framerate = 0;
        float m_frame_time = 0.0f;

        glm::vec4 square_color = { 0.2f, 0.3f, 0.8f, 1.0f };


        //Panels
        SceneHierarchyPanel m_scene_hierarchy_panel;

        void render_camera_selector();


    };
}