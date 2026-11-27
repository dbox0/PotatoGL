#pragma once

#include "engine/core/InplaceFunction.h"
#include "engine/scene/Components.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

// Owns the registry and the hierarchy. Top-level objects are children of a hidden root.
class Scene
{
public:
    using Deferred = InplaceFunction<void(Scene&)>;

private:
    entt::registry m_registry;
    entt::entity m_root = entt::null;
    std::vector<Deferred> m_deferred;
    std::vector<Deferred> m_flushing;  // kept around so neither vector reallocates every frame

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

    // First CameraComponent not tagged Disabled; uses WorldTransform, so update those first
    entt::entity gameCamera() const;
    std::optional<glm::mat4> gameCameraViewProj(float aspect) const;

    // For structural changes that aren't safe mid-iteration (spawning, reparenting)
    template<typename F>
    void defer(F&& fn) { m_deferred.emplace_back(std::forward<F>(fn)); }
    void flushDeferred();

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
