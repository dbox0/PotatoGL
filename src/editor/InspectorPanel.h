#pragma once

#include "engine/reflect/ComponentRegistry.h"

#include <entt/entity/entity.hpp>

#include <cstddef>
#include <optional>

class EditHistory;
class Scene;

class InspectorPanel
{
private:
    // Applied after drawing so no widget is holding a pointer into a removed component
    const ComponentInfo* m_pendingAdd = nullptr;
    const ComponentInfo* m_pendingRemove = nullptr;
    // Value when the active widget was grabbed; the edit is recorded when it's let go
    std::optional<FieldValue> m_editStart;
    EditHistory* m_history = nullptr;  // valid during draw()

    void drawComponent(const ComponentInfo& info, Scene& scene, entt::entity e);
    void drawAddMenu(const char* button, bool scripts, const ComponentRegistry& components, Scene& scene,
                     entt::entity e);

public:
    // selected: the active object; selectionCount: how many are selected in all
    void draw(Scene& scene, const ComponentRegistry& components, entt::entity selected, EditHistory& history,
              std::size_t selectionCount = 1);
};
