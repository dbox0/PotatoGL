#include "Systems.h"

#include "game/Components.h"
#include "engine/scene/Scene.h"
#include "engine/scene/Transform.h"

#include <entt/entt.hpp>

#include <vector>

void movementSystem(entt::registry& registry, float dt)
{
    for (auto [e, transform, velocity] : registry.view<Transform, Velocity>(entt::exclude<Disabled>).each())
        transform.translate(velocity.value * dt);
}

void spinSystem(entt::registry& registry, float dt)
{
    for (auto [e, transform, spin] : registry.view<Transform, Spin>(entt::exclude<Disabled>).each())
        transform.rotate(spin.radiansPerSec * dt, glm::vec3(0.0f, 1.0f, 0.0f));
}

void lifetimeSystem(entt::registry& registry, float dt)
{
    for (auto [e, lifetime] : registry.view<Lifetime>(entt::exclude<Dead>).each()) {
        lifetime.remaining -= dt;
        if (lifetime.remaining <= 0.0f)
            registry.emplace<Dead>(e);
    }
}

void cleanupSystem(Scene& scene)
{
    entt::registry& registry = scene.registry();
    auto dead = registry.view<Dead>();
    if (dead.begin() == dead.end())
        return;

    // Destroying a parent also destroys its children, which may be in this list too
    std::vector<entt::entity> toDestroy(dead.begin(), dead.end());
    for (const entt::entity e : toDestroy) {
        if (registry.valid(e))
            scene.destroyRecursive(e);
    }
}
