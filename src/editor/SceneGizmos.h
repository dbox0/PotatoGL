#pragma once

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <vector>

class Scene;
class Selection;
struct ImDrawList;

// Scene view overlay for objects with no mesh: an icon on every light and camera, and the
// light's range / cone or the camera's frustum on selected ones. Icons can be clicked.
class SceneGizmos
{
private:
    struct Icon
    {
        entt::entity entity;
        glm::vec2 pos;
        float depth;  // clip w, nearer wins a pick
    };
    std::vector<Icon> m_icons;

public:
    struct View
    {
        glm::mat4 viewProj;
        glm::vec2 origin;  // screen rect of the image
        glm::vec2 size;
        float gameAspect;  // for camera frustums
    };

    void draw(ImDrawList* drawList, const Scene& scene, const Selection& selection, const View& view);
    // Icon under the mouse from the last draw(), entt::null if none
    entt::entity pick(const glm::vec2& mouse) const;
};
