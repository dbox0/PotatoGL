#pragma once

#include "engine/gfx/RenderTarget.h"
#include "engine/ui/UiComponents.h"

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <utility>
#include <vector>

class EditHistory;
class Selection;
struct OverlayContext;

// 2D editor for the selected object's Canvas (or the first one), laid out at a preview
// resolution. Left click selects, drag moves, the handles resize, the pivot dot and anchor
// triangles drag too; arrows nudge (Shift: 10). Snaps to the parent, siblings and an
// optional grid; Ctrl turns snapping off. Wheel zooms, middle drag or Space + drag pans, F frames.
class CanvasPanel
{
public:
    static constexpr const char* WindowName = "Canvas";

private:
    enum class Drag
    {
        None,
        Move,
        Resize,
        Pivot,
        Anchor,
        Pan,
    };

    struct DragItem
    {
        entt::entity entity;
        RectTransform start;
        UIRect startRect;
        UIRect parentRect;
        glm::mat2 parentInverse;  // target pixels to the parent's layout space
    };

    RenderTarget m_target;
    entt::entity m_canvas = entt::null;
    int m_preset = 0;
    glm::ivec2 m_custom{1600, 900};
    glm::ivec2 m_resolution{1920, 1080};
    float m_zoom = 1.0f;       // screen points per preview pixel
    glm::vec2 m_origin{0.0f};  // the preview's top-left, relative to the view
    glm::vec2 m_viewPos{0.0f};
    glm::vec2 m_viewSize{0.0f};
    bool m_frame = true;
    bool m_open = true;
    bool m_visible = false;
    bool m_focusRequested = false;
    bool m_snap = true;
    bool m_grid = false;
    float m_gridSize = 10.0f;  // in the parent's layout units

    Drag m_drag = Drag::None;
    int m_handle = -1;  // Resize: HandleDirs index; Anchor: corner 0..3
    glm::vec2 m_dragStart{0.0f};  // preview pixels
    std::vector<DragItem> m_items;
    std::vector<std::pair<glm::vec2, glm::vec2>> m_guides;  // snap lines, preview pixels

    glm::vec2 toView(glm::vec2 previewPixel) const { return m_viewPos + m_origin + previewPixel * m_zoom; }
    glm::vec2 toPreview(glm::vec2 viewPoint) const { return (viewPoint - m_viewPos - m_origin) / m_zoom; }
    void frame();
    int handleAt(const UIRect& rect, glm::vec2 mouse) const;
    int anchorAt(const OverlayContext& context, entt::entity e, glm::vec2 mouse) const;
    bool pivotAt(const OverlayContext& context, entt::entity e, glm::vec2 mouse) const;
    bool beginDrag(const OverlayContext& context, const Selection& selection, Drag drag, glm::vec2 mouse);
    void updateDrag(const OverlayContext& context, glm::vec2 mouse, bool keepAspect, bool fromCentre, bool snap);
    void endDrag(const OverlayContext& context, EditHistory& history);
    void nudge(const OverlayContext& context, EditHistory& history, const Selection& selection, glm::vec2 step);
    // Snap lines along axis from the parent and from item's siblings that aren't being dragged
    std::vector<float> snapTargets(const OverlayContext& context, const DragItem& item, int axis) const;
    void drawOverlay(const OverlayContext& context, const Selection& selection, entt::entity hovered) const;

public:
    void draw(const OverlayContext& context, EditHistory& history, Selection& selection);
    void render(const OverlayContext& context);  // after draw(), with the other views
    void release() { m_target.release(); }
    void focus() { m_open = m_focusRequested = true; }
    bool& open() { return m_open; }
    bool visible() const { return m_visible; }
};
