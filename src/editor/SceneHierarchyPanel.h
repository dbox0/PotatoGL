#pragma once

#include <entt/entity/entity.hpp>

#include <vector>

class ComponentRegistry;
class EditHistory;
class Scene;
class SceneActions;
class Selection;

// Click selects, Ctrl+click adds or removes, Shift+click selects a range. Dragging a
// selected object moves the whole selection. Rows have an icon for what the object is;
// prefab instances are drawn in blue.
class SceneHierarchyPanel
{
private:
    Selection* m_selection = nullptr;     // valid during draw()
    SceneActions* m_actions = nullptr;    // valid during draw()
    entt::entity m_revealed = entt::null;  // last active object the tree was opened up to
    bool m_reveal = false;
    bool m_focused = false;

    entt::entity m_anchor = entt::null;   // where a Shift+click range starts
    entt::entity m_clickedSelected = entt::null;  // plain click on a selected object: narrows on release
    std::vector<entt::entity> m_rows;      // visible rows in order, for Shift+click
    std::vector<entt::entity> m_drawnRows;

    // Tree edits are deferred until after drawing so the walk never sees a changing hierarchy
    entt::entity m_reparentChild = entt::null;
    entt::entity m_reparentTarget = entt::null;

    // inPrefab: e is inside a prefab instance
    void drawNode(Scene& scene, entt::entity e, bool inPrefab);
    void click(entt::entity e);
    void applyPendingEdits(Scene& scene, const ComponentRegistry& components, EditHistory& history);

public:
    void draw(Scene& scene, const ComponentRegistry& components, EditHistory& history, Selection& selection,
              SceneActions& actions);
    bool focused() const { return m_focused; }
};
