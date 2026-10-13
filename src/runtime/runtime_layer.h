#pragma once
#include <Honey.h>

namespace Honey {
    class RuntimeLayer : public Layer {

    public:
        RuntimeLayer();
        virtual ~RuntimeLayer() = default;

        void on_attach() override;
        void on_detach() override;

        void on_update(Timestep ts) override;
        void on_event(Event& event) override;

    private:
        bool on_key_pressed(KeyPressedEvent& e);
        bool on_mouse_button_pressed(MouseButtonPressedEvent& e);

        Ref<Scene> open_scene(const std::filesystem::path& path);

        Ref<Scene> m_active_scene;
        glm::vec2 m_viewport_size = {1680.0f, 720.0f};
        bool m_viewport_maximized = false;


    };
}