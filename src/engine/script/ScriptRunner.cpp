#include "ScriptRunner.h"

#include "engine/script/ScriptRegistry.h"

#include <algorithm>

ScriptRunner::~ScriptRunner()
{
    for (auto& entry : m_entries)
        entry->disconnect(m_scene.registry(), *entry);
}

void ScriptRunner::registerAll()
{
    std::vector<ScriptRegistration> registrations = scriptRegistrations();
    std::sort(registrations.begin(), registrations.end(),
              [](const ScriptRegistration& a, const ScriptRegistration& b) { return a.name < b.name; });
    for (const ScriptRegistration& r : registrations)
        r.add(*this);
}

void ScriptRunner::runStarts()
{
    if (!m_anyPendingStart)
        return;
    m_anyPendingStart = false;
    for (auto& entry : m_entries) {
        if (entry->pendingStart.empty())
            continue;
        std::vector<entt::entity> pending;
        pending.swap(entry->pendingStart);
        for (const entt::entity e : pending)
            entry->start(m_scene, e);
    }
}

void ScriptRunner::run(HookFn Entry::*hook, float dt)
{
    runStarts();
    for (auto& entry : m_entries) {
        if (const HookFn fn = (*entry).*hook)
            fn(m_scene, dt);
    }
    m_scene.flushDeferred();
}
