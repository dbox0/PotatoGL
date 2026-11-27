#pragma once

#include "engine/core/InplaceFunction.h"
#include "engine/scene/Components.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
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
    std::string m_path;                // file it was loaded from / saved to; empty if never
    std::unordered_map<std::uint64_t, entt::entity> m_byUuid;

    // index = position among the parent's children; SIZE_MAX or past the end appends
    void link(entt::entity e, entt::entity parent, std::size_t index = SIZE_MAX);
    void unlink(entt::entity e);
    void onRelationshipDestroy(entt::registry& registry, entt::entity e);
    void onUuidDestroy(entt::registry& registry, entt::entity e);
    void updateWorld(entt::entity e, const glm::mat4& parentWorld);

public:
    Scene();
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    entt::registry& registry()             { return m_registry; }
    const entt::registry& registry() const { return m_registry; }
    entt::entity root() const              { return m_root; }

    // parent = entt::null puts it at the top level. uuid = 0, or one already in use, gets a
    // fresh one; loading passes the saved one.
    entt::entity createGameObject(std::string name, entt::entity parent = entt::null, std::uint64_t uuid = 0,
                                  std::size_t index = SIZE_MAX);

    // A plain registry().destroy() moves the children up to the top level instead
    void destroyRecursive(entt::entity e);
    // Destroys every game object and drops deferred work; the root stays
    void clear();

    const std::string& path() const     { return m_path; }
    void setPath(std::string path)      { m_path = std::move(path); }

    // Keeps the local transform. Fails if parent is e or one of its descendants.
    // index = position among the new parent's children; SIZE_MAX appends.
    bool setParent(entt::entity e, entt::entity parent, std::size_t index = SIZE_MAX);
    bool isAncestor(entt::entity ancestor, entt::entity e) const;
    entt::entity parentOf(entt::entity e) const { return m_registry.get<Relationship>(e).parent; }
    std::size_t siblingIndex(entt::entity e) const;

    // 0 for the root and for entities without a Uuid
    std::uint64_t uuidOf(entt::entity e) const;
    // 0 gives the root; entt::null if not found
    entt::entity findByUuid(std::uint64_t uuid) const;

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
