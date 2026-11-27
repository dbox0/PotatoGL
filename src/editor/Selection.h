#pragma once

#include <entt/entity/registry.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

// The editor's selected objects. The last one is active: the inspector shows it and the
// gizmo sits on it.
class Selection
{
private:
    std::vector<entt::entity> m_items;

public:
    const std::vector<entt::entity>& items() const { return m_items; }
    bool empty() const                             { return m_items.empty(); }
    std::size_t size() const                       { return m_items.size(); }
    entt::entity active() const                    { return m_items.empty() ? entt::null : m_items.back(); }

    bool contains(entt::entity e) const { return std::find(m_items.begin(), m_items.end(), e) != m_items.end(); }

    void clear() { m_items.clear(); }
    void set(entt::entity e)
    {
        m_items.clear();
        if (e != entt::null)
            m_items.push_back(e);
    }
    void assign(std::vector<entt::entity> items) { m_items = std::move(items); }
    // Also makes e the active one
    void add(entt::entity e)
    {
        remove(e);
        if (e != entt::null)
            m_items.push_back(e);
    }
    void remove(entt::entity e) { std::erase(m_items, e); }
    void toggle(entt::entity e)
    {
        if (contains(e))
            remove(e);
        else
            add(e);
    }
    void prune(const entt::registry& registry)
    {
        std::erase_if(m_items, [&](entt::entity e) { return !registry.valid(e); });
    }
};
