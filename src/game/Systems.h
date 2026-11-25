#pragma once

#include <entt/entity/fwd.hpp>

class Scene;

void movementSystem(entt::registry& registry, float dt);
void spinSystem(entt::registry& registry, float dt);
void lifetimeSystem(entt::registry& registry, float dt);

// Destroys everything tagged Dead; runs last so no system sees a half-destroyed entity
void cleanupSystem(Scene& scene);
