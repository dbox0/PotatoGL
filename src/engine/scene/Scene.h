#pragma once

#include "engine/scene/Components.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string>

// Owns the registry and the hierarchy. Top-level objects are children of a hidden root.
class Scene
{
private:
    entt::registry m_registry;
    entt::entity m_root = entt::null;

    void link(entt::entity e, entt::entity parent);
    void unlink(entt::entity e);
    void onRelationshipDestroy(entt::registry& registry, entt::entity e);
    void updateWorld(entt::entity e, const glm::mat4& parentWorld);

public:
    Scene();
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    entt::registry& registry()             { return m_registry; }
    const entt::registry& registry() const { return m_registry; }
    entt::entity root() const              { return m_root; }

    // parent = entt::null puts it at the top level
    entt::entity createGameObject(std::string name, entt::entity parent = entt::null);

    // A plain registry().destroy() moves the children up to the top level instead
    void destroyRecursive(entt::entity e);

    // Keeps the local transform. Fails if parent is e or one of its descendants.
    bool setParent(entt::entity e, entt::entity parent);
    bool isAncestor(entt::entity ancestor, entt::entity e) const;

    void updateWorldTransforms();

    template<typename Fn>
    void forEachChild(entt::entity e, Fn&& fn) const
    {
        entt::entity child = m_registry.get<Relationship>(e).firstChild;
        while (child != entt::null) {
            const entt::entity next = m_registry.get<Relationship>(child).nextSibling;
            fn(child);
            child = next;
        }
    }
};
