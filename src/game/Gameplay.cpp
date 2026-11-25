#include "Gameplay.h"

#include "game/Components.h"
#include "game/Systems.h"
#include "game/scripts/Register.h"
#include "game/scripts/Rotator.h"

#include "engine/assets/Model.h"
#include "engine/core/Paths.h"
#include "engine/reflect/ComponentRegistry.h"
#include "engine/render/Renderer.h"
#include "engine/scene/Camera.h"
#include "engine/scene/Scene.h"
#include "engine/scene/Transform.h"

#include <algorithm>
#include <string>

bool Gameplay::init(const GameContext& context)
{
    Scene& scene = context.scene;
    entt::registry& registry = scene.registry();

    registerGameComponents(context.components);
    m_scripts = std::make_unique<ScriptRunner>(scene, context.components);
    registerScripts(*m_scripts);

    std::optional<ModelData> duck = loadModel(resourcePath("assets/Duck.glb"));
    if (!duck)
        return false;
    const ModelId duckModel = context.renderer.addModel(*duck);

    const entt::entity ducks = scene.createGameObject("Ducks");

    constexpr int gridSize = 10;
    constexpr float spacing = 2.5f;
    for (int z = 0; z < gridSize; ++z) {
        for (int x = 0; x < gridSize; ++x) {
            const glm::vec3 pos((x - (gridSize - 1) * 0.5f) * spacing, 0.0f, (z - (gridSize - 1) * 0.5f) * spacing);
            const float yaw = glm::radians(static_cast<float>((x * 37 + z * 61) % 360));
            const entt::entity e = scene.createGameObject("Duck " + std::to_string(z * gridSize + x), ducks);
            Transform& transform = registry.get<Transform>(e);
            transform.setPos(pos);
            transform.rotate(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            registry.emplace<MeshRenderer>(e, duckModel);
            registry.emplace<Spin>(e, 0.5f + static_cast<float>((x * 13 + z * 7) % 16) * 0.1f);

            if (x == 0 && z == 0) {
                const entt::entity duckling = scene.createGameObject("Duckling", e);
                registry.get<Transform>(duckling).setPos({0.0f, 1.5f, 0.0f});
                registry.get<Transform>(duckling).setScale(glm::vec3(0.5f));
                registry.emplace<MeshRenderer>(duckling, duckModel);
                registry.emplace<Rotator>(duckling, glm::vec3(1.0f, 0.0f, 0.0f), 180.0f);
            }
        }
    }

    context.camera.getTransform().setPos({0.0f, 12.0f, 22.0f});
    context.camera.getTransform().lookAt({0.0f, 0.0f, 0.0f});
    return true;
}

// Gameplay runs at a fixed 60 Hz regardless of frame rate. The clamp stops a long stall
// from triggering a burst of catch-up steps.
void Gameplay::update(const GameContext& context, float dt)
{
    m_scripts->update(dt);

    m_accumulator = std::min(m_accumulator + dt, 0.25f);
    while (m_accumulator >= FixedStep) {
        fixedUpdate(context, FixedStep);
        m_accumulator -= FixedStep;
    }

    m_scripts->lateUpdate(dt);
}

void Gameplay::fixedUpdate(const GameContext& context, float dt)
{
    entt::registry& registry = context.scene.registry();
    movementSystem(registry, dt);
    spinSystem(registry, dt);
    lifetimeSystem(registry, dt);
    m_scripts->fixedUpdate(dt);
    cleanupSystem(context.scene);
}

// Entities belong to the Scene and models to the Renderer; only the script hooks are ours
void Gameplay::shutdown(const GameContext&)
{
    m_scripts.reset();
}
