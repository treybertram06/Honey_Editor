#pragma once

#include <Honey.h>
#include <glm/glm.hpp>

class BirdController : public Honey::ScriptableEntity {
public:
    void on_create() override {}

    void on_destroy() override {}

    void on_update(Honey::Timestep ts) override {
        using namespace Honey;

        // make this member variable?
        auto& transform = get_component<TransformComponent>();

        transform.translation.y += m_velocity * ts;

        float max_rotation = 45.0f; // degrees
        float min_rotation = -90.0f; // degrees
        float rotation = glm::clamp(m_velocity * 5.0f, min_rotation, max_rotation);
        transform.rotation.z = glm::radians(rotation);

        transform.rotation.z = glm::mix(transform.rotation.z, glm::radians(rotation), ts * 5.0f);

        if (m_flying)
            m_velocity -= 9.81f * ts; // gravity

        if (Input::is_key_pressed(KeyCode::Space)) {
            if (!m_flying)
                m_flying = true;

            m_velocity = 5.0f; // flap impulse


        }


    }

private:
    float m_velocity = 0.0f;
    bool m_flying = false;
};

REGISTER_SCRIPT(BirdController)
