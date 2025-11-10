#pragma once

#include <Honey.h>

#include "Honey/scene/script_registry.h"

class CameraController : public Honey::ScriptableEntity {
public:
    void on_create() {
    }

    void on_destroy() {}

    void on_update(Honey::Timestep ts) {
        auto& translation = get_component<Honey::TransformComponent>().translation;
        float speed = 5.0f * ts;

        if (Honey::Input::is_key_pressed(Honey::KeyCode::A))
            translation.x -= speed;
        if (Honey::Input::is_key_pressed(Honey::KeyCode::D))
            translation.x += speed;
        if (Honey::Input::is_key_pressed(Honey::KeyCode::S))
            translation.y -= speed;
        if (Honey::Input::is_key_pressed(Honey::KeyCode::W))
            translation.y += speed;
    }

};

REGISTER_SCRIPT(CameraController)