#pragma once

#include "engine/gfx/RenderTarget.h"
#include "engine/ui/UiComponents.h"

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <vector>

class EditHistory;
class Selection;
struct OverlayContext;

// 2D editor for the selected object's Canvas (or the first one), laid out at a preview
// resolution. Left click selects, drag moves, the handles resize; wheel zooms, middle drag
// or Space + left drag pans, F frames.
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
        Pan,
    };

    struct DragItem
    {
        entt::entity entity;
        RectTransform start;
        glm::mat2 parentInverse;  // target pixels to the parent's layout space
    };

    RenderTarget m_target;
    entt::entity m_canvas = entt::null;
    int m_preset = 0;
    glm::ivec2 m_custom{1600, 900};
    glm::ivec2 m_resolution{1920, 1080};
    float m_zoom = 1.0f;          // screen points per preview pixel
    glm::vec2 m_origin{0.0f};     // the preview's top-left, relative to the view
    glm::vec2 m_viewPos{0.0f};
    glm::vec2 m_viewSize{0.0f};
    bool m_frame = true;
    bool m_visible = false;

    Drag m_drag = Drag::None;
    int m_handle = -1;
    glm::vec2 m_dragStart{0.0f};  // preview pixels
    UIRect m_startRect;           // the active element's, for resizing
    std::vector<DragItem> m_items;

    glm::vec2 toView(glm::vec2 previewPixel) const { return m_viewPos + m_origin + previewPixel * m_zoom; }
    glm::vec2 toPreview(glm::vec2 viewPoint) const { return (viewPoint - m_viewPos - m_origin) / m_zoom; }
    void frame();
    int handleAt(const UIRect& rect, glm::vec2 mouse) const;
    void beginDrag(const OverlayContext& context, const Selection& selection, Drag drag, glm::vec2 mouse);
    void updateDrag(const OverlayContext& context, glm::vec2 mouse, bool keepAspect, bool fromCentre);
    void endDrag(const OverlayContext& context, EditHistory& history);
    void drawOverlay(const OverlayContext& context, const Selection& selection, entt::entity hovered) const;

public:
    void draw(const OverlayContext& context, EditHistory& history, Selection& selection);
    void render(const OverlayContext& context);  // after draw(), with the other views
    void release() { m_target.release(); }
    bool visible() const { return m_visible; }
};
