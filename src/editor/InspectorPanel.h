#pragma once

#include <entt/entity/entity.hpp>

class ComponentRegistry;
class Scene;
struct ComponentInfo;

class InspectorPanel
{
private:
    // Applied after drawing so no widget is holding a pointer into a removed component
    const ComponentInfo* m_pendingAdd = nullptr;
    const ComponentInfo* m_pendingRemove = nullptr;

    void drawComponent(const ComponentInfo& info, Scene& scene, entt::entity e);
    void drawAddMenu(const char* button, bool scripts, const ComponentRegistry& components, Scene& scene,
                     entt::entity e);

public:
    void draw(Scene& scene, const ComponentRegistry& components, entt::entity selected);
};
