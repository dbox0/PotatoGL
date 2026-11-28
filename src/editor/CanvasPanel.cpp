#include "CanvasPanel.h"

#include "editor/EditCommands.h"
#include "editor/EditHistory.h"
#include "editor/SceneActions.h"
#include "editor/Selection.h"

#include "engine/app/Overlay.h"
#include "engine/gfx/GpuProfiler.h"
#include "engine/reflect/ComponentRegistry.h"
#include "engine/render/Renderer.h"
#include "engine/render/UIRenderer.h"
#include "engine/scene/Components.h"
#include "engine/scene/Scene.h"
#include "engine/ui/UiHitTest.h"
#include "engine/ui/UiLayout.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>

namespace
{
struct Preset
{
    const char* name;
    int width, height;
};

constexpr Preset Presets[] = {
    {"1920 x 1080 (16:9)", 1920, 1080},
    {"1280 x 720 (16:9)", 1280, 720},
    {"2560 x 1440 (16:9)", 2560, 1440},
    {"1024 x 768 (4:3)", 1024, 768},
    {"2560 x 1080 (21:9)", 2560, 1080},
    {"1080 x 1920 (Portrait)", 1080, 1920},
    {"Custom", 0, 0},
};
constexpr int CustomPreset = static_cast<int>(std::size(Presets)) - 1;

// Corners first, so they win where handles overlap on a small rect
const glm::ivec2 HandleDirs[8] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {0, -1}, {1, 0}, {0, 1}, {-1, 0}};
constexpr float HandleRadius = 5.0f;
constexpr float SnapPoints = 6.0f;  // screen points

// The pivot moves inside a fixed rect; the position compensates for the rotation and scale around it
RectTransform withPivot(const RectTransform& rt, const UIRect& start, glm::vec2 pivot)
{
    const glm::vec2 size = start.max - start.min;
    const glm::mat2 rs = uiPivotLinear(rt.rotationDeg, rt.scale);
    const glm::vec2 oldPoint = start.min + size * rt.pivot;
    const glm::vec2 newPoint = start.min + size * pivot;
    const glm::vec2 shift = (glm::mat2(1.0f) - rs) * (oldPoint - newPoint);
    const glm::vec2 span = size - rt.sizeDelta;
    const glm::vec2 aMin = oldPoint - rt.anchoredPosition - span * rt.pivot;
    RectTransform out = rt;
    out.pivot = pivot;
    out.anchoredPosition = newPoint + shift - (aMin + span * pivot);
    return out;
}

// New anchors with the rect left where it is
RectTransform withAnchors(const RectTransform& rt, const UIRect& rect, const UIRect& parent, glm::vec2 anchorMin,
                          glm::vec2 anchorMax)
{
    const glm::vec2 size = rect.max - rect.min;
    const glm::vec2 parentSize = parent.max - parent.min;
    const glm::vec2 aMin = parent.min + anchorMin * parentSize;
    const glm::vec2 aMax = parent.min + anchorMax * parentSize;
    RectTransform out = rt;
    out.anchorMin = anchorMin;
    out.anchorMax = anchorMax;
    out.sizeDelta = size - (aMax - aMin);
    out.anchoredPosition = rect.min + size * rt.pivot - (aMin + (aMax - aMin) * rt.pivot);
    return out;
}

// Closest target to any of points within threshold: the offset to add, or false
bool closest(const std::vector<float>& targets, std::initializer_list<float> points, float threshold, float& offset,
             float& target)
{
    bool found = false;
    for (const float p : points) {
        for (const float t : targets) {
            if (std::abs(t - p) < threshold) {
                threshold = std::abs(t - p);
                offset = t - p;
                target = t;
                found = true;
            }
        }
    }
    return found;
}

// 0, 0.5 and 1 within threshold
float snapUnit(float v, float threshold)
{
    for (const float t : {0.0f, 0.5f, 1.0f}) {
        if (std::abs(v - t) < threshold)
            return t;
    }
    return v;
}

float axisScale(const UIRect& rect, int axis)
{
    return std::max(glm::length(glm::vec2(rect.toScreen[axis])), 1e-6f);
}

glm::vec2 rectPoint(const UIRect& rect, glm::vec2 dir)
{
    const glm::vec2 centre = (rect.min + rect.max) * 0.5f;
    const glm::vec2 local = centre + dir * (rect.max - rect.min) * 0.5f;
    return glm::vec2(rect.toScreen * glm::vec3(local, 1.0f));
}

ImVec2 im(glm::vec2 v) { return ImVec2(v.x, v.y); }

// The image holds premultiplied alpha; ImGui's own blending assumes straight alpha
void premultipliedBlend(const ImDrawList*, const ImDrawCmd*)
{
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

// Drags one edge or corner of a rect in its own layout space by d, the opposite side staying put.
// The pivot moves with the size, so the position compensates for the rotation and scale around it.
RectTransform resized(const RectTransform& rt, const UIRect& start, glm::ivec2 dir, glm::vec2 d, bool keepAspect,
                      bool fromCentre)
{
    const glm::vec2 size = start.max - start.min;
    glm::vec2 newSize = size, fixed(0.0f), t(0.5f);
    for (int i = 0; i < 2; ++i) {
        const float h = static_cast<float>(dir[i]);
        if (dir[i] == 0) {
            fixed[i] = (start.min[i] + start.max[i]) * 0.5f;
        } else if (fromCentre) {
            newSize[i] = size[i] + 2.0f * h * d[i];
            fixed[i] = (start.min[i] + start.max[i]) * 0.5f;
        } else {
            newSize[i] = size[i] + h * d[i];
            fixed[i] = dir[i] > 0 ? start.min[i] : start.max[i];
            t[i] = dir[i] > 0 ? 0.0f : 1.0f;
        }
    }
    if (keepAspect && size.x > 0.0f && size.y > 0.0f) {
        const glm::vec2 f = newSize / size;
        const float k = dir.x == 0                                     ? f.y
                        : dir.y == 0                                   ? f.x
                        : std::abs(f.x - 1.0f) > std::abs(f.y - 1.0f) ? f.x
                                                                       : f.y;
        newSize = size * k;
    }
    newSize = glm::max(newSize, glm::vec2(0.0f));
    const glm::vec2 newMin = fixed - newSize * t;

    const glm::mat2 rs = uiPivotLinear(rt.rotationDeg, rt.scale);
    const glm::vec2 oldPivot = start.min + size * rt.pivot;
    const glm::vec2 newPivot = newMin + newSize * rt.pivot;
    const glm::vec2 shift = (glm::mat2(1.0f) - rs) * (oldPivot - newPivot);
    const glm::vec2 anchorPoint = oldPivot - rt.anchoredPosition;

    RectTransform out = rt;
    out.sizeDelta = newSize - (size - rt.sizeDelta);
    out.anchoredPosition = newPivot + shift - anchorPoint;
    return out;
}
}  // namespace

void CanvasPanel::frame()
{
    const glm::vec2 res(m_resolution);
    if (m_viewSize.x < 1.0f || m_viewSize.y < 1.0f)
        return;
    m_zoom = std::min(m_viewSize.x / res.x, m_viewSize.y / res.y) * 0.9f;
    m_origin = (m_viewSize - res * m_zoom) * 0.5f;
    m_frame = false;
}

int CanvasPanel::handleAt(const UIRect& rect, glm::vec2 mouse) const
{
    for (int i = 0; i < 8; ++i) {
        const glm::vec2 p = toView(rectPoint(rect, glm::vec2(HandleDirs[i])));
        if (std::abs(p.x - mouse.x) <= HandleRadius + 1.0f && std::abs(p.y - mouse.y) <= HandleRadius + 1.0f)
            return i;
    }
    return -1;
}

static glm::vec2 anchorPoint(const RectTransform& rt, const UIRect& parent, int corner)
{
    const glm::vec2 a((corner == 1 || corner == 2) ? rt.anchorMax.x : rt.anchorMin.x,
                      corner >= 2 ? rt.anchorMax.y : rt.anchorMin.y);
    const glm::vec2 local = parent.min + a * (parent.max - parent.min);
    return glm::vec2(parent.toScreen * glm::vec3(local, 1.0f));
}

int CanvasPanel::anchorAt(const OverlayContext& context, entt::entity e, glm::vec2 mouse) const
{
    const entt::registry& registry = context.scene.registry();
    const UIRect* parent = registry.try_get<UIRect>(context.scene.parentOf(e));
    if (!parent)
        return -1;
    const RectTransform& rt = registry.get<RectTransform>(e);
    for (int corner = 0; corner < 4; ++corner) {
        const glm::vec2 sign((corner == 1 || corner == 2) ? 1.0f : -1.0f, corner >= 2 ? 1.0f : -1.0f);
        const glm::vec2 tip = toView(anchorPoint(rt, *parent, corner)) + sign * 5.0f;
        if (glm::length(tip - mouse) <= 7.0f)
            return corner;
    }
    return -1;
}

bool CanvasPanel::pivotAt(const OverlayContext& context, entt::entity e, glm::vec2 mouse) const
{
    const entt::registry& registry = context.scene.registry();
    const UIRect& rect = registry.get<UIRect>(e);
    const glm::vec2 local = rect.min + (rect.max - rect.min) * registry.get<RectTransform>(e).pivot;
    return glm::length(toView(glm::vec2(rect.toScreen * glm::vec3(local, 1.0f))) - mouse) <= 7.0f;
}

bool CanvasPanel::beginDrag(const OverlayContext& context, const Selection& selection, Drag drag, glm::vec2 mouse)
{
    const Scene& scene = context.scene;
    const entt::registry& registry = scene.registry();
    m_items.clear();
    const auto add = [&](entt::entity e) {
        if (e == entt::null || !registry.valid(e) || !registry.all_of<RectTransform, UIRect>(e) ||
            uiCanvasOf(scene, e) != m_canvas)
            return;
        const UIRect* parentRect = registry.try_get<UIRect>(scene.parentOf(e));
        if (!parentRect)
            return;
        m_items.push_back({e, registry.get<RectTransform>(e), registry.get<UIRect>(e), *parentRect,
                           glm::inverse(glm::mat2(parentRect->toScreen))});
    };
    add(selection.active());  // first: the one snapping is measured on
    if (drag == Drag::Move) {
        const EditContext edit{context.scene, context.components};
        for (const entt::entity e : selectionRoots(edit, selection)) {
            if (e != selection.active())
                add(e);
        }
    }
    m_drag = m_items.empty() ? Drag::None : drag;
    m_dragStart = mouse;
    return m_drag != Drag::None;
}

std::vector<float> CanvasPanel::snapTargets(const OverlayContext& context, const DragItem& item, int axis) const
{
    const Scene& scene = context.scene;
    const entt::registry& registry = scene.registry();
    std::vector<float> targets = {item.parentRect.min[axis], (item.parentRect.min[axis] + item.parentRect.max[axis]) * 0.5f,
                                  item.parentRect.max[axis]};
    scene.forEachChild(scene.parentOf(item.entity), [&](entt::entity sibling) {
        if (registry.all_of<Disabled>(sibling) || !registry.all_of<RectTransform, UIRect>(sibling))
            return;
        for (const DragItem& dragged : m_items) {
            if (dragged.entity == sibling)
                return;
        }
        const UIRect& rect = registry.get<UIRect>(sibling);
        targets.push_back(rect.min[axis]);
        targets.push_back((rect.min[axis] + rect.max[axis]) * 0.5f);
        targets.push_back(rect.max[axis]);
    });
    return targets;
}

void CanvasPanel::updateDrag(const OverlayContext& context, glm::vec2 mouse, bool keepAspect, bool fromCentre, bool snap)
{
    entt::registry& registry = context.scene.registry();
    m_guides.clear();
    const DragItem& primary = m_items.front();
    const UIRect& parent = primary.parentRect;
    const auto guide = [&](int axis, float at) {
        glm::vec2 a = parent.min, b = parent.max;
        a[axis] = b[axis] = at;
        m_guides.emplace_back(glm::vec2(parent.toScreen * glm::vec3(a, 1.0f)), glm::vec2(parent.toScreen * glm::vec3(b, 1.0f)));
    };
    const auto snapAxis = [&](int axis, std::initializer_list<float> points, float gridFrom, float& offset) {
        const float threshold = SnapPoints / (m_zoom * axisScale(parent, axis));
        float target = 0.0f;
        if (closest(snapTargets(context, primary, axis), points, threshold, offset, target)) {
            guide(axis, target);
            return;
        }
        if (m_grid && m_gridSize > 0.0f) {
            const float from = gridFrom - parent.min[axis];
            offset = std::round(from / m_gridSize) * m_gridSize - from;
        }
    };

    if (m_drag == Drag::Move) {
        const glm::vec2 screenDelta = mouse - m_dragStart;
        glm::vec2 d = primary.parentInverse * screenDelta;
        if (snap && primary.start.rotationDeg == 0.0f) {
            for (int axis = 0; axis < 2; ++axis) {
                const float lo = primary.startRect.min[axis] + d[axis], hi = primary.startRect.max[axis] + d[axis];
                float offset = 0.0f;
                snapAxis(axis, {lo, (lo + hi) * 0.5f, hi}, lo, offset);
                d[axis] += offset;
            }
        }
        const glm::vec2 screen = glm::mat2(parent.toScreen) * d;
        for (const DragItem& item : m_items) {
            if (RectTransform* rt = registry.try_get<RectTransform>(item.entity))
                rt->anchoredPosition = item.start.anchoredPosition + item.parentInverse * screen;
        }
        return;
    }

    RectTransform* rt = registry.try_get<RectTransform>(primary.entity);
    if (!rt)
        return;
    const RectTransform& start = primary.start;
    const UIRect& rect = primary.startRect;
    if (m_drag == Drag::Resize) {
        glm::vec2 d = uiToLocal(rect, mouse) - uiToLocal(rect, m_dragStart);
        const glm::ivec2 dir = HandleDirs[m_handle];
        if (snap && !fromCentre && start.rotationDeg == 0.0f && start.scale == glm::vec2(1.0f)) {
            for (int axis = 0; axis < 2; ++axis) {
                if (dir[axis] == 0)
                    continue;
                const float edge = (dir[axis] > 0 ? rect.max[axis] : rect.min[axis]) + d[axis];
                float offset = 0.0f;
                snapAxis(axis, {edge}, edge, offset);
                d[axis] += offset;
            }
        }
        *rt = resized(start, rect, dir, d, keepAspect, fromCentre);
    } else if (m_drag == Drag::Pivot) {
        const glm::vec2 size = rect.max - rect.min;
        if (size.x == 0.0f || size.y == 0.0f)
            return;
        glm::vec2 pivot = (uiToLocal(rect, mouse) - rect.min) / size;
        if (snap) {
            for (int axis = 0; axis < 2; ++axis)
                pivot[axis] = snapUnit(pivot[axis], SnapPoints / (m_zoom * axisScale(rect, axis) * std::abs(size[axis])));
        }
        *rt = withPivot(start, rect, pivot);
    } else if (m_drag == Drag::Anchor) {
        const glm::vec2 parentSize = parent.max - parent.min;
        if (parentSize.x == 0.0f || parentSize.y == 0.0f)
            return;
        glm::vec2 a = glm::clamp((uiToLocal(parent, mouse) - parent.min) / parentSize, glm::vec2(0.0f), glm::vec2(1.0f));
        if (snap) {
            for (int axis = 0; axis < 2; ++axis)
                a[axis] = snapUnit(a[axis], SnapPoints / (m_zoom * axisScale(parent, axis) * std::abs(parentSize[axis])));
        }
        glm::vec2 anchorMin = start.anchorMin, anchorMax = start.anchorMax;
        const bool maxSide[2] = {m_handle == 1 || m_handle == 2, m_handle >= 2};
        for (int axis = 0; axis < 2; ++axis) {
            if (start.anchorMin[axis] == start.anchorMax[axis])
                anchorMin[axis] = anchorMax[axis] = a[axis];
            else if (maxSide[axis])
                anchorMax[axis] = std::max(a[axis], anchorMin[axis]);
            else
                anchorMin[axis] = std::min(a[axis], anchorMax[axis]);
        }
        *rt = withAnchors(start, rect, parent, anchorMin, anchorMax);
    }
}

void CanvasPanel::endDrag(const OverlayContext& context, EditHistory& history)
{
    entt::registry& registry = context.scene.registry();
    const ComponentInfo* info = context.components.find("RectTransform");
    EditContext edit{context.scene, context.components};
    const char* verb = m_drag == Drag::Resize ? "Resize" : m_drag == Drag::Pivot ? "Move Pivot"
                       : m_drag == Drag::Anchor ? "Move Anchors" : "";
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (const DragItem& item : m_items) {
        if (!info || !registry.valid(item.entity) || !registry.all_of<RectTransform>(item.entity))
            continue;
        if (auto command = componentEditCommand(edit, item.entity, *info, &item.start,
                                                &registry.get<RectTransform>(item.entity), verb))
            commands.push_back(std::move(command));
    }
    if (commands.size() == 1)
        history.push(std::move(commands.front()));
    else if (!commands.empty())
        history.push(groupCommand("Move " + std::to_string(commands.size()) + " Objects", std::move(commands)));
    m_items.clear();
    m_guides.clear();
    m_drag = Drag::None;
}

void CanvasPanel::nudge(const OverlayContext& context, EditHistory& history, const Selection& selection, glm::vec2 step)
{
    entt::registry& registry = context.scene.registry();
    const ComponentInfo* info = context.components.find("RectTransform");
    if (!info)
        return;
    EditContext edit{context.scene, context.components};
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (const entt::entity e : selectionRoots(edit, selection)) {
        if (!registry.all_of<RectTransform>(e) || uiCanvasOf(context.scene, e) != m_canvas)
            continue;
        RectTransform& rt = registry.get<RectTransform>(e);
        const RectTransform before = rt;
        rt.anchoredPosition += step;
        if (auto command = componentEditCommand(edit, e, *info, &before, &rt, "Nudge"))
            commands.push_back(std::move(command));
    }
    if (commands.size() == 1)
        history.push(std::move(commands.front()));
    else if (!commands.empty())
        history.push(groupCommand("Nudge " + std::to_string(commands.size()) + " Objects", std::move(commands)));
}

void CanvasPanel::draw(const OverlayContext& context, EditHistory& history, Selection& selection)
{
    if (!m_open) {
        m_visible = false;
        return;
    }
    if (m_focusRequested) {
        ImGui::SetNextWindowFocus();
        m_focusRequested = false;
    }
    m_visible = ImGui::Begin(WindowName, &m_open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!m_visible) {
        if (m_drag != Drag::None && m_drag != Drag::Pan)
            endDrag(context, history);
        m_drag = Drag::None;
        ImGui::End();
        return;
    }
    Scene& scene = context.scene;
    entt::registry& registry = scene.registry();

    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("##resolution", Presets[m_preset].name)) {
        for (int i = 0; i < static_cast<int>(std::size(Presets)); ++i) {
            if (ImGui::Selectable(Presets[i].name, i == m_preset)) {
                m_preset = i;
                m_frame = true;
            }
        }
        ImGui::EndCombo();
    }
    if (m_preset == CustomPreset) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::InputInt2("##custom", &m_custom.x, ImGuiInputTextFlags_EnterReturnsTrue))
            m_frame = true;
        m_custom = glm::clamp(m_custom, glm::ivec2(16), glm::ivec2(8192));
        m_resolution = m_custom;
    } else {
        m_resolution = {Presets[m_preset].width, Presets[m_preset].height};
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame"))
        m_frame = true;
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &m_snap);
    ImGui::SetItemTooltip("To the parent, siblings and the grid. Hold Ctrl to turn it off while dragging.");
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &m_grid);
    if (m_grid) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.0f);
        ImGui::DragFloat("##gridSize", &m_gridSize, 0.5f, 1.0f, 500.0f, "%.0f");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%.0f%%", m_zoom * 100.0f);

    m_canvas = uiCanvasOf(scene, selection.active());
    if (m_canvas == entt::null) {
        const std::vector<entt::entity> canvases = uiCanvases(scene);
        m_canvas = canvases.empty() ? entt::null : canvases.front();
    }
    if (m_canvas == entt::null) {
        ImGui::TextDisabled("No Canvas in the scene: GameObject > UI > Canvas");
        ImGui::End();
        return;
    }
    uiLayoutSystem(scene, glm::vec2(m_resolution));

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1.0f || avail.y < 1.0f) {
        ImGui::End();
        return;
    }
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    m_viewPos = {cursor.x, cursor.y};
    m_viewSize = {avail.x, avail.y};
    if (m_frame)
        frame();
    ImGui::InvisibleButton("##view", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);

    if (hovered && io.MouseWheel != 0.0f) {
        const glm::vec2 under = toPreview(mouse);
        m_zoom = std::clamp(m_zoom * std::pow(1.15f, io.MouseWheel), 0.02f, 32.0f);
        m_origin = mouse - m_viewPos - under * m_zoom;
    }
    if (ImGui::IsWindowFocused() && !io.WantTextInput && m_drag == Drag::None) {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false))
            frame();
        const float step = io.KeyShift ? 10.0f : 1.0f;
        glm::vec2 move(0.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            move.x -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            move.x += step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            move.y -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            move.y += step;
        if (move != glm::vec2(0.0f))
            nudge(context, history, selection, move);
    }

    const bool space = ImGui::IsKeyDown(ImGuiKey_Space);
    if (m_drag == Drag::None && hovered &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (space && ImGui::IsMouseClicked(ImGuiMouseButton_Left))))
        m_drag = Drag::Pan;
    if (m_drag == Drag::Pan) {
        m_origin += glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_drag = Drag::None;
    }

    const glm::vec2 pixel = toPreview(mouse);
    if (m_drag == Drag::None && hovered && !space && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const entt::entity active = selection.active();
        const bool activeHere = active != entt::null && registry.valid(active) &&
                                registry.all_of<RectTransform, UIRect>(active) && uiCanvasOf(scene, active) == m_canvas;
        const int handle = activeHere && !io.KeyCtrl ? handleAt(registry.get<UIRect>(active), mouse) : -1;
        const int anchor = activeHere && !io.KeyCtrl && handle < 0 ? anchorAt(context, active, mouse) : -1;
        if (handle >= 0) {
            m_handle = handle;
            beginDrag(context, selection, Drag::Resize, pixel);
        } else if (activeHere && !io.KeyCtrl && pivotAt(context, active, mouse)) {
            beginDrag(context, selection, Drag::Pivot, pixel);
        } else if (anchor >= 0) {
            m_handle = anchor;
            beginDrag(context, selection, Drag::Anchor, pixel);
        } else {
            const entt::entity hit = uiHitTest(scene, m_canvas, pixel);
            if (io.KeyCtrl || io.KeyShift) {
                if (hit != entt::null)
                    selection.toggle(hit);
            } else if (hit == entt::null) {
                selection.clear();
            } else {
                if (selection.contains(hit))
                    selection.add(hit);
                else
                    selection.set(hit);
                beginDrag(context, selection, Drag::Move, pixel);
            }
        }
    }
    if (m_drag != Drag::None && m_drag != Drag::Pan) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            updateDrag(context, pixel, io.KeyShift, io.KeyAlt, m_snap && !io.KeyCtrl);
        else
            endDrag(context, history);
    }

    const entt::entity under = hovered && m_drag == Drag::None ? uiHitTest(scene, m_canvas, pixel) : entt::null;
    drawOverlay(context, selection, under);
    ImGui::End();
}

void CanvasPanel::drawOverlay(const OverlayContext& context, const Selection& selection, entt::entity hovered) const
{
    const Scene& scene = context.scene;
    const entt::registry& registry = scene.registry();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const glm::vec2 viewMax = m_viewPos + m_viewSize;
    draw->PushClipRect(im(m_viewPos), im(viewMax), true);
    draw->AddRectFilled(im(m_viewPos), im(viewMax), IM_COL32(36, 36, 38, 255));

    const glm::vec2 a = toView(glm::vec2(0.0f)), b = toView(glm::vec2(m_resolution));
    const glm::vec2 lo = glm::max(a, m_viewPos), hi = glm::min(b, viewMax);
    if (lo.x < hi.x && lo.y < hi.y) {
        constexpr float cell = 12.0f;
        draw->AddRectFilled(im(lo), im(hi), IM_COL32(92, 92, 92, 255));
        const float x0 = a.x + std::floor((lo.x - a.x) / cell) * cell;
        const float y0 = a.y + std::floor((lo.y - a.y) / cell) * cell;
        for (float y = y0; y < hi.y; y += cell) {
            for (float x = x0; x < hi.x; x += cell) {
                if ((static_cast<int>(std::lround((x - a.x) / cell)) + static_cast<int>(std::lround((y - a.y) / cell))) & 1)
                    continue;
                draw->AddRectFilled(ImVec2(std::max(x, lo.x), std::max(y, lo.y)),
                                    ImVec2(std::min(x + cell, hi.x), std::min(y + cell, hi.y)), IM_COL32(72, 72, 72, 255));
            }
        }
    }
    if (m_target.valid()) {
        draw->AddCallback(premultipliedBlend, nullptr);
        draw->AddImage(static_cast<ImTextureID>(m_target.displayTexture()), im(a), im(b), ImVec2(0.0f, 1.0f),
                       ImVec2(1.0f, 0.0f));
        draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }
    draw->AddRect(im(a), im(b), IM_COL32(255, 255, 255, 110));

    if (m_grid && m_gridSize > 0.0f && registry.all_of<UIRect>(m_canvas)) {
        const float step = m_gridSize * axisScale(registry.get<UIRect>(m_canvas), 0) * m_zoom;
        if (step >= 6.0f) {
            for (float x = a.x; x <= b.x; x += step)
                draw->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), IM_COL32(255, 255, 255, 22));
            for (float y = a.y; y <= b.y; y += step)
                draw->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), IM_COL32(255, 255, 255, 22));
        }
    }

    const auto outline = [&](const UIRect& rect, ImU32 color, float thickness) {
        ImVec2 points[4];
        const glm::vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int i = 0; i < 4; ++i)
            points[i] = im(toView(rectPoint(rect, corners[i])));
        draw->AddPolyline(points, 4, color, ImDrawFlags_Closed, thickness);
    };
    if (hovered != entt::null && !selection.contains(hovered))
        outline(registry.get<UIRect>(hovered), IM_COL32(120, 180, 255, 140), 1.0f);
    for (const entt::entity e : selection.items()) {
        if (!registry.valid(e) || !registry.all_of<RectTransform, UIRect>(e) || uiCanvasOf(scene, e) != m_canvas)
            continue;
        outline(registry.get<UIRect>(e), IM_COL32(60, 150, 255, 255), e == selection.active() ? 2.0f : 1.0f);
    }
    for (const auto& [from, to] : m_guides)
        draw->AddLine(im(toView(from)), im(toView(to)), IM_COL32(255, 80, 200, 220), 1.0f);

    const entt::entity active = selection.active();
    if (active != entt::null && registry.valid(active) && registry.all_of<RectTransform, UIRect>(active) &&
        uiCanvasOf(scene, active) == m_canvas) {
        const UIRect& rect = registry.get<UIRect>(active);
        const RectTransform& rt = registry.get<RectTransform>(active);
        if (const UIRect* parent = registry.try_get<UIRect>(scene.parentOf(active))) {
            for (int corner = 0; corner < 4; ++corner) {
                const glm::vec2 sign((corner == 1 || corner == 2) ? 1.0f : -1.0f, corner >= 2 ? 1.0f : -1.0f);
                const glm::vec2 p = toView(anchorPoint(rt, *parent, corner));
                const ImVec2 tri[3] = {im(p), im(p + glm::vec2(sign.x * 10.0f, sign.y * 4.0f)),
                                       im(p + glm::vec2(sign.x * 4.0f, sign.y * 10.0f))};
                draw->AddTriangleFilled(tri[0], tri[1], tri[2], IM_COL32(240, 240, 240, 230));
                draw->AddTriangle(tri[0], tri[1], tri[2], IM_COL32(40, 40, 40, 255));
            }
        }
        for (const glm::ivec2 dir : HandleDirs) {
            const glm::vec2 p = toView(rectPoint(rect, glm::vec2(dir)));
            draw->AddRectFilled(ImVec2(p.x - HandleRadius, p.y - HandleRadius), ImVec2(p.x + HandleRadius, p.y + HandleRadius),
                                IM_COL32(255, 255, 255, 255));
            draw->AddRect(ImVec2(p.x - HandleRadius, p.y - HandleRadius), ImVec2(p.x + HandleRadius, p.y + HandleRadius),
                          IM_COL32(60, 150, 255, 255));
        }
        const glm::vec2 pivot =
            toView(glm::vec2(rect.toScreen * glm::vec3(rect.min + (rect.max - rect.min) * rt.pivot, 1.0f)));
        draw->AddCircleFilled(im(pivot), 4.0f, IM_COL32(255, 255, 255, 200), 12);
        draw->AddCircle(im(pivot), 4.0f, IM_COL32(60, 150, 255, 255), 12, 2.0f);
    }
    draw->PopClipRect();
}

void CanvasPanel::render(const OverlayContext& context)
{
    const entt::registry& registry = context.scene.registry();
    if (!m_visible || m_canvas == entt::null || !registry.valid(m_canvas))
        return;
    m_target.resize(m_resolution.x, m_resolution.y);
    if (!m_target.valid())
        return;
    GpuScope scope(context.renderer.profiler(), "Canvas View");
    const glm::vec2 size(m_resolution);
    uiLayoutSystem(context.scene, size);
    m_target.bind();
    glViewport(0, 0, m_resolution.x, m_resolution.y);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    context.ui.begin(size);
    context.ui.drawCanvas(context.scene, m_canvas);
    context.ui.end(&m_target, m_resolution.x, m_resolution.y);
}
