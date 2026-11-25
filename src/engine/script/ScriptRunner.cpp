#include "ScriptRunner.h"

ScriptRunner::~ScriptRunner()
{
    for (auto& entry : m_entries)
        entry->disconnect(m_scene.registry(), *entry);
}

void ScriptRunner::runStarts()
{
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
