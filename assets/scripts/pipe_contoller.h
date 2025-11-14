#pragma once

#include <Honey.h>
#include <glm/glm.hpp>

class PipeController : public Honey::ScriptableEntity {
public:

    void on_create() override {}

    void on_destroy() override {}

    void on_update(Honey::Timestep ts) override {
        using namespace Honey;

        auto& transform = get_component<TransformComponent>();
        transform.translation.x -= m_speed * ts;

    }

private:
    float m_speed = 1.0f;
};

