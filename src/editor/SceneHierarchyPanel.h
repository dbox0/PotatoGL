#pragma once

#include <entt/entity/entity.hpp>

class Scene;

class SceneHierarchyPanel
{
private:
    entt::entity m_selected = entt::null;

    // Tree edits are deferred until after drawing so the walk never sees a changing hierarchy
    entt::entity m_reparentChild = entt::null;
    entt::entity m_reparentTarget = entt::null;
    entt::entity m_destroyTarget = entt::null;
    entt::entity m_createParent = entt::null;
    bool m_createRequested = false;

    void drawNode(Scene& scene, entt::entity e);
    void applyPendingEdits(Scene& scene);

public:
    void draw(Scene& scene);

    entt::entity selected() const { return m_selected; }
};
