#pragma once
#include <Honey.h>

#include "Honey/renderer/scene_viewport_renderer.h"

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
        Ref<Scene> open_scene(const std::filesystem::path& path);

        Ref<Scene> m_active_scene;

        SceneViewportRenderer m_scene_viewport_renderer;
        glm::vec2 m_viewport_size = {1280.0f, 720.0f};
    };
}
