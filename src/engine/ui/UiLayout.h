#pragma once

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <vector>

class Scene;
struct Canvas;

// Top-level canvases (no Canvas ancestor) that aren't Disabled, in draw order: by sortOrder
std::vector<entt::entity> uiCanvases(const Scene& scene);

// The top-level canvas e is (or is under); entt::null if none
entt::entity uiCanvasOf(const Scene& scene, entt::entity e);

// The rotation and scale part of a RectTransform's transform around its pivot
glm::mat2 uiPivotLinear(float rotationDeg, glm::vec2 scale);

float canvasScale(const Canvas& canvas, glm::vec2 screenSize);

// Writes a UIRect on every canvas and on every RectTransform below one. Disabled entities,
// and entities without a RectTransform, are skipped with their subtree.
void uiLayoutSystem(Scene& scene, glm::vec2 screenSize);
