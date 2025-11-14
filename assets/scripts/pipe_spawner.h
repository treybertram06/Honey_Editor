#pragma once

#include <Honey.h>
#include <glm/glm.hpp>

#include "pipe_contoller.h"

class PipeSpawner : public Honey::ScriptableEntity {
public:

    // I think the ability to spawn something like a pipe prefab would be good here
    void spawn_pipe_pair() {
        auto* scene = get_scene();

        float gap = 10.0f; // vertical gap between pipes
        float xPos = 5.0f;
        float yOffset = ((float)rand() / RAND_MAX) * 2.0f - 1.0f; // random between -1 and 1

        // Bottom pipe
        auto bottomPipe = scene->create_entity("PipeBottom");
        auto& bottomTransform = bottomPipe.get_component<Honey::TransformComponent>();
        bottomTransform.translation = { xPos, yOffset - gap / 2.0f, 0.0f };
        bottomTransform.scale = { 0.1f, 10.0f, 1.0f };
        auto& bottomSprite = bottomPipe.add_component<Honey::SpriteRendererComponent>();
        auto& bottomScript = bottomPipe.add_component<Honey::NativeScriptComponent>();
        bottomScript.bind<PipeController>();

        // Top pipe
        auto topPipe = scene->create_entity("PipeTop");
        auto& topTransform = topPipe.get_component<Honey::TransformComponent>();
        topTransform.translation = { xPos, yOffset + gap / 2.0f, 0.0f };
        topTransform.scale = { 0.1f, 1.0f, 1.0f };
        auto& topSprite = topPipe.add_component<Honey::SpriteRendererComponent>();
        auto& topScript = topPipe.add_component<Honey::NativeScriptComponent>();
        topScript.bind<PipeController>();
    }

    void on_create() override {

    }

    void on_destroy() override {}

    void on_update(Honey::Timestep ts) override {
        using namespace Honey;

        m_timer += ts;

        if ( m_timer >= 1.0f ) {
            m_timer = 0.0f;
            spawn_pipe_pair();
        }

        auto* scene = get_scene();
        float speed = 2.0f * ts;

        // Move and clean up pipes
        for (auto it = m_active_pipes.begin(); it != m_active_pipes.end();) {
            auto& transform = it->get_component<TransformComponent>();
            transform.translation.x -= speed;

            if (transform.translation.x < -5.0f) {
                scene->destroy_entity(*it);
                it = m_active_pipes.erase(it);
            } else {
                ++it;
            }
        }



    }

private:
    std::vector<Honey::Entity> m_active_pipes;
    int m_pipe_index = 0;

    float m_timer = 0.0f;
};

REGISTER_SCRIPT(PipeSpawner)
