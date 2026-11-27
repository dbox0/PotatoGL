#pragma once

#include "editor/SceneGizmos.h"
#include "editor/ViewportPanel.h"

#include "engine/gfx/OutlineMask.h"
#include "engine/gfx/PickBuffer.h"
#include "engine/scene/Transform.h"

#include <entt/entity/entity.hpp>

#include <vector>

class EditHistory;
class Selection;
struct OverlayContext;

// Scene view: editor camera, click to select (Ctrl/Shift+click adds or removes), Unity-style
// transform gizmo on the active object that moves the whole selection, selection outline,
// light and camera gizmos. Q none, W move, E rotate, R scale, X local/global, hold Ctrl to snap.
class SceneViewPanel
{
public:
    enum class Tool
    {
        None,
        Move,
        Rotate,
        Scale,
    };

private:
    ViewportPanel m_view{"Scene"};
    PickBuffer m_pick;
    OutlineMask m_outline;
    Tool m_tool = Tool::Move;
    bool m_local = true;

    SceneGizmos m_gizmos;

    bool m_pickRequested = false;
    bool m_pickAdditive = false;   // Ctrl or Shift was held: toggle instead of replace
    int m_pickX = 0, m_pickY = 0;  // pick buffer pixel, bottom-left origin

    // The selection roots as they were when the current gizmo drag started; the end of a
    // drag is one undo step
    struct DragItem
    {
        entt::entity entity;
        Transform start;
        glm::mat4 startWorld;
    };
    std::vector<DragItem> m_drag;
    entt::entity m_dragActive = entt::null;
    glm::mat4 m_dragActiveStart{1.0f};
    glm::vec3 m_dragActiveScale{1.0f};

    void drawToolbar();
    void handleHotkeys(const OverlayContext& context);
    void drawGizmo(const OverlayContext& context, EditHistory& history, const Selection& selection);
    void beginDrag(const OverlayContext& context, const Selection& selection);
    void endDrag(const OverlayContext& context, EditHistory& history);

public:
    // gameAspect: for the selected cameras' frustums
    void draw(const OverlayContext& context, EditHistory& history, Selection& selection, float gameAspect);
    // After the scene is updated: color, ids on a click, then the selection outline
    void render(const OverlayContext& context, Selection& selection);
    void release();

    void focus()                        { m_view.focus(); }
    const char* name() const            { return m_view.name(); }
    bool hovered() const                { return m_view.hovered(); }
    bool focused() const                { return m_view.focused(); }
};
