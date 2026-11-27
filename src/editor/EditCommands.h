#pragma once

#include "editor/EditHistory.h"

#include "engine/reflect/ComponentRegistry.h"

#include <entt/entity/entity.hpp>

#include <memory>
#include <vector>

class Transform;

// Already applied: one field changed from before to after (an inspector edit)
std::unique_ptr<EditCommand> setFieldCommand(Scene& scene, entt::entity e, const ComponentInfo& info,
                                             const FieldInfo& field, const FieldValue& before,
                                             const FieldValue& after);
// Already applied: a gizmo drag
std::unique_ptr<EditCommand> transformCommand(EditContext& context, entt::entity e, const Transform& before,
                                              const Transform& after);

// Not yet applied: execute() adds / removes the component
std::unique_ptr<EditCommand> addComponentCommand(Scene& scene, entt::entity e, const ComponentInfo& info);
std::unique_ptr<EditCommand> removeComponentCommand(Scene& scene, entt::entity e, const ComponentInfo& info);

// Already applied: e was just created
std::unique_ptr<EditCommand> createdEntityCommand(Scene& scene, entt::entity e);
// Not yet applied: execute() destroys e and its descendants
std::unique_ptr<EditCommand> destroyEntityCommand(Scene& scene, entt::entity e);
// Not yet applied: execute() moves e under parent (entt::null = top level), appended
std::unique_ptr<EditCommand> reparentCommand(Scene& scene, entt::entity e, entt::entity parent);

// Several commands as one step; undone in reverse
std::unique_ptr<EditCommand> groupCommand(std::string name, std::vector<std::unique_ptr<EditCommand>> commands);
