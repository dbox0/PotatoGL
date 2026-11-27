#pragma once

#include "editor/ViewportPanel.h"

#include "engine/gfx/PickBuffer.h"
#include "engine/scene/Transform.h"

#include <entt/entity/entity.hpp>

#include <optional>

class EditHistory;
struct OverlayContext;

// Scene view: editor camera, click to select, Unity-style transform gizmo, selection outline.
// Q none, W move, E rotate, R scale, X local/global, hold Ctrl to snap, Delete removes.
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
    Tool m_tool = Tool::Move;
    bool m_local = true;

    bool m_pickRequested = false;
    int m_pickX = 0, m_pickY = 0;  // pick buffer pixel, bottom-left origin

    // Transform at the start of the current gizmo drag; the end of a drag is one undo step
    std::optional<Transform> m_dragStart;
    entt::entity m_dragEntity = entt::null;

    void drawToolbar();
    void handleHotkeys(const OverlayContext& context, EditHistory& history, entt::entity& selected);
    void drawGizmo(const OverlayContext& context, EditHistory& history, entt::entity selected);

public:
    void draw(const OverlayContext& context, EditHistory& history, entt::entity& selected);
    // After the scene is updated: color, then ids when needed for picking or the outline
    void render(const OverlayContext& context, entt::entity& selected);
    void release();

    void focus()                        { m_view.focus(); }
    const char* name() const            { return m_view.name(); }
    bool hovered() const                { return m_view.hovered(); }
};
