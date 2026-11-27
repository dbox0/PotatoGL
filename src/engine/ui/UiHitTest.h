#pragma once

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <functional>

class Scene;
struct UIRect;

// point in target pixels, like UIRect::toScreen's output
glm::vec2 uiToLocal(const UIRect& rect, glm::vec2 point);
bool uiRectContains(const UIRect& rect, glm::vec2 point);

// The topmost element under point among canvas's descendants (not the canvas itself),
// walked like the renderer: Disabled subtrees and entities without a RectTransform skipped.
// accept narrows which elements count; empty accepts all.
entt::entity uiHitTest(const Scene& scene, entt::entity canvas, glm::vec2 point,
                       const std::function<bool(entt::entity)>& accept = {});
