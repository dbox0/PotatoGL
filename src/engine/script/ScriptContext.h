#pragma once

#include "engine/scene/Components.h"
#include "engine/scene/Scene.h"
#include "engine/scene/Transform.h"

#include <functional>
#include <utility>

// What a script hook gets: its own entity plus the scene. Built on the stack per call.
struct ScriptContext
{
    Scene& scene;
    entt::entity self;

    entt::registry& registry() { return scene.registry(); }

    template<typename T>
    T& get() { return scene.registry().get<T>(self); }

    template<typename T>
    T* tryGet() { return scene.registry().try_get<T>(self); }

    template<typename T>
    bool has() { return scene.registry().all_of<T>(self); }

    // Never add the calling script's own type here; use defer() for that
    template<typename T, typename... Args>
    decltype(auto) add(Args&&... args)
    {
        return scene.registry().emplace_or_replace<T>(self, std::forward<Args>(args)...);
    }

    Transform& transform() { return get<Transform>(); }

    void destroy() { scene.registry().emplace_or_replace<Dead>(self); }
    void defer(std::function<void(Scene&)> fn) { scene.defer(std::move(fn)); }
};
