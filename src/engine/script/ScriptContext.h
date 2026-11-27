#pragma once

#include "engine/core/EventBus.h"
#include "engine/core/Input.h"
#include "engine/scene/Components.h"
#include "engine/scene/Scene.h"
#include "engine/scene/Transform.h"
#include "engine/script/EventListeners.h"

#include <utility>

namespace detail
{
template<typename>
struct MethodClass;

template<typename Class, typename Ret, typename... Args>
struct MethodClass<Ret (Class::*)(Args...)>
{
    using Type = Class;
};
}  // namespace detail

// What a script hook gets: its own entity plus the scene. Built on the stack per call.
struct ScriptContext
{
    using TransformStorage = entt::storage_for_t<Transform>;

    Scene& scene;
    entt::entity self;

private:
    TransformStorage* m_transforms;
    // Stays valid for one hook: Transforms are only added mid-hook (pages don't move),
    // never removed, since destruction is deferred
    Transform* m_transform = nullptr;

public:
    ScriptContext(Scene& s, entt::entity e, TransformStorage* transforms = nullptr)
        : scene(s), self(e), m_transforms(transforms) {}

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

    Transform& transform()
    {
        if (!m_transform)
            m_transform = m_transforms ? &m_transforms->get(self) : &get<Transform>();
        return *m_transform;
    }
    const Input& input() { return scene.registry().ctx().get<Input>(); }

    void destroy() { scene.registry().emplace_or_replace<Dead>(self); }

    EventBus* events() { return scene.registry().ctx().find<EventBus>(); }

    // Listeners run now
    template<typename E>
    void emit(const E& e)
    {
        if (EventBus* bus = events())
            bus->emit(e);
    }

    // Delivered when App flushes the bus, after the game update
    template<typename E>
    void enqueue(E e)
    {
        if (EventBus* bus = events())
            bus->enqueue(std::move(e));
    }

    // Calls (script.*Method)(ctx, event) on this entity's script while it exists and isn't
    // Disabled: ctx.on<PlayerDamaged, &Hud::onDamaged>(). Call from onStart, not onCreate,
    // so loading a scene in Edit mode doesn't subscribe.
    template<typename E, auto Method>
    void on()
    {
        using Script = typename detail::MethodClass<decltype(Method)>::Type;
        EventBus* bus = events();
        if (!bus)
            return;
        Scene* target = &scene;
        const entt::entity e = self;
        Connection connection = bus->on<E>([target, e](const E& event) {
            entt::registry& r = target->registry();
            if (!r.valid(e) || r.all_of<Disabled>(e))
                return;
            if (Script* script = r.try_get<Script>(e)) {
                ScriptContext ctx{*target, e};
                (script->*Method)(ctx, event);
            }
        });
        scene.registry().get_or_emplace<EventListeners>(self).entries.push_back(
            {entt::type_hash<Script>::value(), std::move(connection)});
    }

    // Captures must fit Scene::Deferred (64 bytes); no heap allocation
    template<typename F>
    void defer(F&& fn) { scene.defer(std::forward<F>(fn)); }
};
