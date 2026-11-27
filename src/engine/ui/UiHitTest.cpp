#include "UiHitTest.h"

#include "engine/scene/Components.h"
#include "engine/scene/Scene.h"
#include "engine/ui/UiComponents.h"

#include <algorithm>

glm::vec2 uiToLocal(const UIRect& rect, glm::vec2 point)
{
    return glm::vec2(glm::inverse(rect.toScreen) * glm::vec3(point, 1.0f));
}

bool uiRectContains(const UIRect& rect, glm::vec2 point)
{
    const glm::vec2 p = uiToLocal(rect, point);
    const glm::vec2 lo = glm::min(rect.min, rect.max), hi = glm::max(rect.min, rect.max);
    return p.x >= lo.x && p.y >= lo.y && p.x <= hi.x && p.y <= hi.y;
}

static void hitChildren(const Scene& scene, entt::entity parent, glm::vec2 point,
                        const std::function<bool(entt::entity)>& accept, entt::entity& hit)
{
    const entt::registry& registry = scene.registry();
    scene.forEachChild(parent, [&](entt::entity child) {
        if (registry.all_of<Disabled>(child) || !registry.all_of<RectTransform, UIRect>(child))
            return;
        if (uiRectContains(registry.get<UIRect>(child), point) && (!accept || accept(child)))
            hit = child;
        hitChildren(scene, child, point, accept, hit);
    });
}

entt::entity uiHitTest(const Scene& scene, entt::entity canvas, glm::vec2 point,
                       const std::function<bool(entt::entity)>& accept)
{
    entt::entity hit = entt::null;
    if (canvas != entt::null && scene.registry().valid(canvas))
        hitChildren(scene, canvas, point, accept, hit);
    return hit;
}
