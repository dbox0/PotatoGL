#pragma once

#include "engine/core/EventBus.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <vector>

// A script's event subscriptions, made with ScriptContext::on(). Runtime only, never saved;
// destroying it disconnects them, and removing a script disconnects that script's own.
struct EventListeners
{
    struct Entry
    {
        entt::id_type script;  // entt::type_hash of the script type
        Connection connection;
    };
    std::vector<Entry> entries;

    EventListeners() = default;
    EventListeners(const EventListeners&) = delete;
    EventListeners& operator=(const EventListeners&) = delete;
    EventListeners(EventListeners&&) noexcept = default;
    EventListeners& operator=(EventListeners&&) noexcept = default;

    void removeScript(entt::id_type script)
    {
        std::erase_if(entries, [script](const Entry& entry) { return entry.script == script; });
    }
};
