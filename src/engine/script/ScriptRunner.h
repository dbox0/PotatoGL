#pragma once

#include "engine/reflect/ComponentRegistry.h"
#include "engine/scene/Components.h"
#include "engine/scene/Scene.h"
#include "engine/script/ScriptContext.h"

#include <entt/entt.hpp>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

// A script is a plain struct stored as a component. Every hook is optional:
//   void onCreate(ScriptContext&);              when added
//   void onStart(ScriptContext&);               before its first update
//   void onUpdate(ScriptContext&, float dt);    every frame
//   void onFixedUpdate(ScriptContext&, float);  every fixed step
//   void onLateUpdate(ScriptContext&, float);   after all updates
//   void onDestroy(ScriptContext&);             when removed or its entity is destroyed

template<typename T>
concept HasOnCreate = requires(T& t, ScriptContext& c) { t.onCreate(c); };
template<typename T>
concept HasOnStart = requires(T& t, ScriptContext& c) { t.onStart(c); };
template<typename T>
concept HasOnDestroy = requires(T& t, ScriptContext& c) { t.onDestroy(c); };
template<typename T>
concept HasOnUpdate = requires(T& t, ScriptContext& c, float dt) { t.onUpdate(c, dt); };
template<typename T>
concept HasOnFixedUpdate = requires(T& t, ScriptContext& c, float dt) { t.onFixedUpdate(c, dt); };
template<typename T>
concept HasOnLateUpdate = requires(T& t, ScriptContext& c, float dt) { t.onLateUpdate(c, dt); };

// One loop per script type per hook: contiguous storage, direct (inlinable) calls.
// A type without a hook gets no loop for it.
class ScriptRunner
{
private:
    using HookFn = void (*)(Scene&, float);

    struct Entry
    {
        Scene* scene = nullptr;
        int order = 0;
        HookFn update = nullptr;
        HookFn fixedUpdate = nullptr;
        HookFn lateUpdate = nullptr;
        void (*start)(Scene&, entt::entity) = nullptr;
        void (*disconnect)(entt::registry&, Entry&) = nullptr;
        std::vector<entt::entity> pendingStart;
    };

    Scene& m_scene;
    ComponentRegistry& m_components;
    std::vector<std::unique_ptr<Entry>> m_entries;  // sorted by order; signals point into them

    template<typename T>
    static void onConstruct(Entry& entry, entt::registry& registry, entt::entity e)
    {
        if constexpr (HasOnCreate<T>) {
            ScriptContext ctx{*entry.scene, e};
            registry.get<T>(e).onCreate(ctx);
        }
        if constexpr (HasOnStart<T>)
            entry.pendingStart.push_back(e);
    }

    template<typename T>
    static void onDestroy(Entry& entry, entt::registry& registry, entt::entity e)
    {
        ScriptContext ctx{*entry.scene, e};
        registry.get<T>(e).onDestroy(ctx);
    }

    void runStarts();
    void run(HookFn Entry::*hook, float dt);

public:
    ScriptRunner(Scene& scene, ComponentRegistry& components) : m_scene(scene), m_components(components) {}
    ~ScriptRunner();

    ScriptRunner(const ScriptRunner&) = delete;
    ScriptRunner& operator=(const ScriptRunner&) = delete;

    // Lower order runs first. Returns the reflection entry so fields can be chained on.
    template<typename T>
    ComponentInfo& add(std::string name, int order = 0);

    void update(float dt)      { run(&Entry::update, dt); }
    void fixedUpdate(float dt) { run(&Entry::fixedUpdate, dt); }
    void lateUpdate(float dt)  { run(&Entry::lateUpdate, dt); }
};

template<typename T>
ComponentInfo& ScriptRunner::add(std::string name, int order)
{
    static_assert(!std::is_empty_v<T>, "a script needs at least one data member");

    auto entry = std::make_unique<Entry>();
    entry->scene = &m_scene;
    entry->order = order;

    if constexpr (HasOnUpdate<T>) {
        entry->update = [](Scene& scene, float dt) {
            for (auto [e, script] : scene.registry().view<T>(entt::exclude<Disabled>).each()) {
                ScriptContext ctx{scene, e};
                script.onUpdate(ctx, dt);
            }
        };
    }
    if constexpr (HasOnFixedUpdate<T>) {
        entry->fixedUpdate = [](Scene& scene, float dt) {
            for (auto [e, script] : scene.registry().view<T>(entt::exclude<Disabled>).each()) {
                ScriptContext ctx{scene, e};
                script.onFixedUpdate(ctx, dt);
            }
        };
    }
    if constexpr (HasOnLateUpdate<T>) {
        entry->lateUpdate = [](Scene& scene, float dt) {
            for (auto [e, script] : scene.registry().view<T>(entt::exclude<Disabled>).each()) {
                ScriptContext ctx{scene, e};
                script.onLateUpdate(ctx, dt);
            }
        };
    }
    if constexpr (HasOnStart<T>) {
        entry->start = [](Scene& scene, entt::entity e) {
            entt::registry& registry = scene.registry();
            if (!registry.valid(e) || !registry.all_of<T>(e))
                return;
            ScriptContext ctx{scene, e};
            registry.get<T>(e).onStart(ctx);
        };
    }

    entt::registry& registry = m_scene.registry();
    if constexpr (HasOnCreate<T> || HasOnStart<T>)
        registry.on_construct<T>().template connect<&ScriptRunner::onConstruct<T>>(*entry);
    if constexpr (HasOnDestroy<T>)
        registry.on_destroy<T>().template connect<&ScriptRunner::onDestroy<T>>(*entry);
    entry->disconnect = [](entt::registry& r, Entry& en) {
        r.on_construct<T>().disconnect(&en);
        r.on_destroy<T>().disconnect(&en);
    };

    auto pos = m_entries.begin();
    while (pos != m_entries.end() && (*pos)->order <= order)
        ++pos;
    m_entries.insert(pos, std::move(entry));

    ComponentInfo& info = m_components.add<T>(std::move(name));
    info.isScript = true;
    return info;
}
