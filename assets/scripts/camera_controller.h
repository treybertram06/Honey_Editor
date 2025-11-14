#pragma once

#include <Honey.h>
#include <glm/glm.hpp>

class CameraController : public Honey::ScriptableEntity {
public:
    void on_create() override {
        m_velocity = {0.0f, 0.0f, 0.0f};
    }

    void on_destroy() override {}

    void on_update(Honey::Timestep ts) override {
        using namespace Honey;

        // Camera transform (this entity)
        auto& camXform = get_component<TransformComponent>();

        // Target: the player's position (center) + optional offset
        Entity player = get_entity_by_tag("Player");
        const auto& playerXform = player.get_component<TransformComponent>();

        // --- Simple damped spring towards target ---
        // target position (follow X/Y, keep camera's current Z)
        glm::vec3 target = playerXform.translation + glm::vec3(offset, 0.0f);

        // Only pull in X/Y; preserve Z so your projection stays sane
        glm::vec3 delta = target - camXform.translation;
        delta.z = 0.0f;
        m_velocity.z = 0.0f;

        // a = k * x - c * v
        glm::vec3 accel = stiffness * delta - damping * m_velocity;

        float dt = (float)ts;
        m_velocity += accel * dt;
        camXform.translation += m_velocity * dt;

        // Optional tiny stabilizer to avoid endless micro-jitter near target
        if (glm::length(delta) < 0.0001f && glm::length(m_velocity) < 0.0001f) {
            m_velocity = {0,0,0};
        }
    }

private:
    glm::vec3 m_velocity{0.0f, 0.0f, 0.0f};

    // Tunables (try these first, then adjust)
    float stiffness = 12.0f;         // how strong the "rope" pulls (higher = snappier)
    float damping   = 8.0f;         // how much it resists overshoot (higher = less bounce)
    glm::vec2 offset{0.0f, 1.0f};   // camera offset from player (X,Y)
};

