#pragma once

#include <Honey.h>
#include <glm/glm.hpp>

class PlayerController : public Honey::ScriptableEntity {
public:
    void on_create() override {
        m_velocity = {0.0f, 0.0f};
        m_grounded = false;
    }

    void on_destroy() override {}

    void on_update(Honey::Timestep ts) override {
        using namespace Honey;

        auto& transform = get_component<TransformComponent>();
        const float dt = (float)ts;

        // Infinite floor height
        Entity floor = get_entity_by_tag("Floor");
        const float floorY = floor.get_component<TransformComponent>().translation.y;

        // Horizontal
        float move = 0.0f;
        if (Input::is_key_pressed(KeyCode::A)) move -= 1.0f;
        if (Input::is_key_pressed(KeyCode::D)) move += 1.0f;
        transform.translation.x += move * m_moveSpeed * dt;

        // Gravity
        m_velocity.y += m_gravity * dt;

        // Jump (only from ground)
        if (m_grounded && Input::is_key_pressed(KeyCode::Space)) {
            m_velocity.y = m_jumpSpeed;
            m_grounded = false;
        }

        // Integrate Y
        transform.translation.y += m_velocity.y * dt;

        // ---- Floor collision (fix) ----
        // Assume scale.y is the full height; half-height is used for center-based transform.
        const float halfH = 0.5f * transform.scale.y;
        const float bottom = transform.translation.y - halfH;

        // If the bottom goes below the floor, snap up so the bottom sits exactly on the floor.
        if (bottom < floorY) {
            transform.translation.y = floorY + halfH;  // snap center to floor top + half height
            m_velocity.y = 0.0f;
            m_grounded = true;
        } else {
            // Only airborne if we're above the floor
            m_grounded = false;
        }
    }

private:
    glm::vec2 m_velocity{0.0f, 0.0f};
    bool m_grounded = false;

    // Tunables
    float m_moveSpeed = 6.0f;
    float m_jumpSpeed = 12.0f;
    float m_gravity   = -25.0f;
};

