#pragma once

#include <Honey.h>
#include <glm/glm.hpp>

class Test : public Honey::ScriptableEntity {
public:
    void on_create() override {

    }

    void on_destroy() override {}

    void on_update(Honey::Timestep ts) override {
        auto& transform = get_component<Honey::TransformComponent>();
        transform.rotation += glm::radians(90.0f) * ts;
        transform.translation.x += 1.0f * ts;
    }


};

