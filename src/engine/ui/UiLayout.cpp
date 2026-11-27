#include "UiLayout.h"

#include "engine/scene/Components.h"
#include "engine/scene/Scene.h"
#include "engine/ui/UiComponents.h"

#include <algorithm>
#include <cmath>

static bool activeInHierarchy(const Scene& scene, entt::entity e)
{
    for (; e != entt::null && e != scene.root(); e = scene.parentOf(e)) {
        if (scene.registry().all_of<Disabled>(e))
            return false;
    }
    return true;
}

static bool underCanvas(const Scene& scene, entt::entity e)
{
    for (e = scene.parentOf(e); e != entt::null && e != scene.root(); e = scene.parentOf(e)) {
        if (scene.registry().all_of<Canvas>(e))
            return true;
    }
    return false;
}

std::vector<entt::entity> uiCanvases(const Scene& scene)
{
    const entt::registry& registry = scene.registry();
    std::vector<entt::entity> canvases;
    for (const entt::entity e : registry.view<Canvas>()) {
        if (!underCanvas(scene, e) && activeInHierarchy(scene, e))
            canvases.push_back(e);
    }
    std::stable_sort(canvases.begin(), canvases.end(), [&](entt::entity a, entt::entity b) {
        return registry.get<Canvas>(a).sortOrder < registry.get<Canvas>(b).sortOrder;
    });
    return canvases;
}

float canvasScale(const Canvas& canvas, glm::vec2 screen)
{
    const glm::vec2 ref = canvas.referenceResolution;
    if (canvas.scaleMode == CanvasScaleMode::ConstantPixelSize || ref.x <= 0.0f || ref.y <= 0.0f ||
        screen.x <= 0.0f || screen.y <= 0.0f)
        return 1.0f;
    const float w = std::log(screen.x / ref.x);
    const float h = std::log(screen.y / ref.y);
    return std::exp(w + (h - w) * std::clamp(canvas.matchWidthOrHeight, 0.0f, 1.0f));
}

// T(pivot) * R * S * T(-pivot). y points down, so a counter-clockwise angle is negative.
static glm::mat3 aroundPivot(glm::vec2 pivot, float degrees, glm::vec2 scale)
{
    const float a = -glm::radians(degrees);
    const float c = std::cos(a), s = std::sin(a);
    glm::mat3 m(1.0f);
    m[0] = glm::vec3(c * scale.x, s * scale.x, 0.0f);
    m[1] = glm::vec3(-s * scale.y, c * scale.y, 0.0f);
    m[2] = glm::vec3(pivot - glm::vec2(m[0]) * pivot.x - glm::vec2(m[1]) * pivot.y, 1.0f);
    return m;
}

static void layoutChildren(Scene& scene, entt::entity parent, const UIRect& parentRect, int& order)
{
    entt::registry& registry = scene.registry();
    scene.forEachChild(parent, [&](entt::entity child) {
        if (registry.all_of<Disabled>(child))
            return;
        const RectTransform* rt = registry.try_get<RectTransform>(child);
        if (!rt)
            return;
        const glm::vec2 parentSize = parentRect.max - parentRect.min;
        const glm::vec2 aMin = parentRect.min + rt->anchorMin * parentSize;
        const glm::vec2 aMax = parentRect.min + rt->anchorMax * parentSize;
        const glm::vec2 size = aMax - aMin + rt->sizeDelta;
        const glm::vec2 pivotPoint = aMin + (aMax - aMin) * rt->pivot + rt->anchoredPosition;

        UIRect rect;
        rect.min = pivotPoint - size * rt->pivot;
        rect.max = rect.min + size;
        rect.toScreen = parentRect.toScreen * aroundPivot(pivotPoint, rt->rotationDeg, rt->scale);
        rect.drawOrder = order++;
        registry.get_or_emplace<UIRect>(child) = rect;
        layoutChildren(scene, child, rect, order);
    });
}

void uiLayoutSystem(Scene& scene, glm::vec2 screenSize)
{
    entt::registry& registry = scene.registry();
    int order = 0;
    for (const entt::entity canvas : uiCanvases(scene)) {
        const float scale = canvasScale(registry.get<Canvas>(canvas), screenSize);
        UIRect rect;
        rect.max = screenSize / scale;
        rect.toScreen = glm::mat3(scale, 0.0f, 0.0f, 0.0f, scale, 0.0f, 0.0f, 0.0f, 1.0f);
        rect.drawOrder = order++;
        registry.get_or_emplace<UIRect>(canvas) = rect;
        layoutChildren(scene, canvas, rect, order);
    }
}
