#pragma once

#include "editor/SceneGizmos.h"
#include "editor/ViewportPanel.h"

#include "engine/gfx/OutlineMask.h"
#include "engine/gfx/PickBuffer.h"
#include "engine/scene/Transform.h"

#include <entt/entity/entity.hpp>

#include <string>
#include <utility>
#include <vector>

class EditHistory;
class SceneActions;
class Selection;
struct OverlayContext;

// Scene view: editor camera, click to select (Ctrl/Shift+click adds or removes), Unity-style
// transform gizmo on the active object that moves the whole selection, selection outline,
// light and camera gizmos, skeletons of selected skinned objects. Q none, W move, E rotate,
// R scale, X local/global, hold Ctrl to snap. Preview plays Animators in Edit mode.
// While a prefab is open it is the Prefab view: the tab says so and a bar offers Save and Back.
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
    ViewportPanel m_view{"Scene", "###SceneView"};
    PickBuffer m_pick;
    OutlineMask m_outline;
    Tool m_tool = Tool::Move;
    bool m_local = true;

    SceneGizmos m_gizmos;

    std::string m_prefab;  // name of the open prefab; empty: the scene
    bool m_prefabDirty = false;
    bool m_saveRequested = false;
    bool m_backRequested = false;

    bool m_pickRequested = false;
    bool m_pickAdditive = false;   // Ctrl or Shift was held: toggle instead of replace
    int m_pickX = 0, m_pickY = 0;  // pick buffer pixel, bottom-left origin

    // A material dropped on the view: assigned to whatever the pick finds under the cursor
    std::string m_dropMaterial;
    entt::entity m_dropTarget = entt::null;

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

    void drawToolbar(const OverlayContext& context);
    void drawPrefabBar();
    void acceptDrops(const OverlayContext& context, SceneActions& actions);
    // Where a ray through the screen point meets the ground (y = 0), or a point in front of the camera
    glm::vec3 dropPoint(const OverlayContext& context, float x, float y) const;
    void handleHotkeys(const OverlayContext& context);
    void drawGizmo(const OverlayContext& context, EditHistory& history, const Selection& selection);
    void beginDrag(const OverlayContext& context, const Selection& selection);
    void endDrag(const OverlayContext& context, EditHistory& history);

public:
    // gameAspect: for the selected cameras' frustums
    void draw(const OverlayContext& context, EditHistory& history, Selection& selection, SceneActions& actions,
              float gameAspect);
    // After the scene is updated: color, ids on a click, then the selection outline
    void render(const OverlayContext& context, Selection& selection);
    void release();

    // name: the open prefab, empty for the scene. Set before draw().
    void setPrefab(const std::string& name, bool dirty);
    bool takeSaveRequest() { return std::exchange(m_saveRequested, false); }
    bool takeBackRequest() { return std::exchange(m_backRequested, false); }

    void focus()                        { m_view.focus(); }
    const char* name() const            { return m_view.name(); }
    bool hovered() const                { return m_view.hovered(); }
    bool focused() const                { return m_view.focused(); }
    // Screen point (ImGui coordinates) is over the rendered image
    bool imageContains(float x, float y) const
    {
        return m_view.visible() && x >= m_view.imageX() && y >= m_view.imageY() &&
               x < m_view.imageX() + m_view.imageWidth() && y < m_view.imageY() + m_view.imageHeight();
    }
    glm::vec3 dropPointAt(const OverlayContext& context, float x, float y) const { return dropPoint(context, x, y); }
};
