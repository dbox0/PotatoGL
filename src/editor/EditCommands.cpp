#include "EditCommands.h"

#include "engine/scene/Scene.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/scene/Transform.h"

#include <cstdint>
#include <string>
#include <utility>

namespace
{
// A live entity from a uuid; entt::null if it's gone. 0 is the root, which edits never target.
entt::entity resolve(Scene& scene, std::uint64_t uuid)
{
    if (uuid == 0)
        return entt::null;
    const entt::entity e = scene.findByUuid(uuid);
    return e != entt::null && scene.registry().valid(e) ? e : entt::null;
}

std::string entityName(Scene& scene, entt::entity e)
{
    const Name* name = scene.registry().try_get<Name>(e);
    return name && !name->value.empty() ? name->value : "Object";
}

class SetFieldCommand final : public EditCommand
{
private:
    std::uint64_t m_entity;
    std::string m_component;
    std::string m_field;
    FieldValue m_before;
    FieldValue m_after;
    std::uint64_t m_beforeRef = 0;  // Entity fields hold uuids, not handles
    std::uint64_t m_afterRef = 0;
    std::string m_label;

    bool apply(EditContext& context, const FieldValue& value, std::uint64_t ref)
    {
        const entt::entity e = resolve(context.scene, m_entity);
        const ComponentInfo* info = context.components.find(m_component);
        if (e == entt::null || !info || !info->has(context.scene.registry(), e))
            return false;
        void* data = info->get(context.scene.registry(), e);
        for (const FieldInfo& field : info->fields) {
            if (field.key != m_field || !field.set)
                continue;
            if (field.type == FieldType::Entity) {
                const entt::entity target = ref == 0 ? entt::null : resolve(context.scene, ref);
                field.set(data, target);
            } else {
                field.set(data, value);
            }
            return true;
        }
        return false;
    }

public:
    SetFieldCommand(Scene& scene, entt::entity e, const ComponentInfo& info, const FieldInfo& field,
                    FieldValue before, FieldValue after)
        : m_entity(scene.uuidOf(e)), m_component(info.id), m_field(field.key), m_before(std::move(before)),
          m_after(std::move(after)), m_label("Edit " + info.name + " " + field.label)
    {
        if (field.type == FieldType::Entity) {
            m_beforeRef = scene.uuidOf(std::get<entt::entity>(m_before));
            m_afterRef = scene.uuidOf(std::get<entt::entity>(m_after));
        }
    }

    bool undo(EditContext& context) override { return apply(context, m_before, m_beforeRef); }
    bool redo(EditContext& context) override { return apply(context, m_after, m_afterRef); }
    std::string name() const override { return m_label; }
};

// Add and remove are the same command with the roles swapped. The component's data is
// captured whenever it's removed, so adding it back restores it exactly.
class ComponentPresenceCommand final : public EditCommand
{
private:
    std::uint64_t m_entity;
    std::string m_component;
    std::string m_data = "{}";
    bool m_addOnRedo;
    std::string m_label;

    bool add(EditContext& context)
    {
        const entt::entity e = resolve(context.scene, m_entity);
        const ComponentInfo* info = context.components.find(m_component);
        if (e == entt::null || !info || info->has(context.scene.registry(), e))
            return false;
        return loadComponent(*info, context.scene, e, m_data);
    }

    bool remove(EditContext& context)
    {
        const entt::entity e = resolve(context.scene, m_entity);
        const ComponentInfo* info = context.components.find(m_component);
        if (e == entt::null || !info || !info->has(context.scene.registry(), e))
            return false;
        m_data = saveComponent(*info, context.scene, e);
        info->remove(context.scene.registry(), e);
        return true;
    }

public:
    ComponentPresenceCommand(Scene& scene, entt::entity e, const ComponentInfo& info, bool addOnRedo)
        : m_entity(scene.uuidOf(e)), m_component(info.id), m_addOnRedo(addOnRedo),
          m_label((addOnRedo ? "Add " : "Remove ") + info.name)
    {
    }

    bool undo(EditContext& context) override { return m_addOnRedo ? remove(context) : add(context); }
    bool redo(EditContext& context) override { return m_addOnRedo ? add(context) : remove(context); }
    std::string name() const override { return m_label; }
};

// Create and destroy, likewise. Destroying captures the whole subtree plus where it sat.
class EntityPresenceCommand final : public EditCommand
{
private:
    std::uint64_t m_entity;
    std::uint64_t m_parent = 0;
    std::size_t m_index = SIZE_MAX;
    std::string m_tree;
    bool m_createOnRedo;
    std::string m_label;

    bool create(EditContext& context)
    {
        if (m_tree.empty() || resolve(context.scene, m_entity) != entt::null)
            return false;
        const entt::entity parent = m_parent == 0 ? entt::null : resolve(context.scene, m_parent);
        if (m_parent != 0 && parent == entt::null)
            return false;
        return loadEntityTree(context.scene, context.components, m_tree, parent, m_index) != entt::null;
    }

    bool destroy(EditContext& context)
    {
        const entt::entity e = resolve(context.scene, m_entity);
        if (e == entt::null)
            return false;
        m_parent = context.scene.uuidOf(context.scene.parentOf(e));
        m_index = context.scene.siblingIndex(e);
        m_tree = saveEntityTree(context.scene, context.components, e);
        context.scene.destroyRecursive(e);
        return true;
    }

public:
    EntityPresenceCommand(Scene& scene, entt::entity e, bool createOnRedo)
        : m_entity(scene.uuidOf(e)), m_createOnRedo(createOnRedo),
          m_label((createOnRedo ? "Create " : "Delete ") + entityName(scene, e))
    {
    }

    bool undo(EditContext& context) override { return m_createOnRedo ? destroy(context) : create(context); }
    bool redo(EditContext& context) override { return m_createOnRedo ? create(context) : destroy(context); }
    std::string name() const override { return m_label; }
};

class ReparentCommand final : public EditCommand
{
private:
    std::uint64_t m_entity;
    std::uint64_t m_fromParent = 0;
    std::size_t m_fromIndex = SIZE_MAX;
    std::uint64_t m_toParent;
    std::size_t m_toIndex = SIZE_MAX;
    std::string m_label;

    bool move(EditContext& context, std::uint64_t parentUuid, std::size_t index, bool recordFrom)
    {
        Scene& scene = context.scene;
        const entt::entity e = resolve(scene, m_entity);
        const entt::entity parent = parentUuid == 0 ? scene.root() : resolve(scene, parentUuid);
        if (e == entt::null || parent == entt::null)
            return false;
        if (recordFrom) {
            m_fromParent = scene.uuidOf(scene.parentOf(e));
            m_fromIndex = scene.siblingIndex(e);
        }
        if (!scene.setParent(e, parent, index))
            return false;
        if (recordFrom)
            m_toIndex = scene.siblingIndex(e);
        return true;
    }

public:
    ReparentCommand(Scene& scene, entt::entity e, entt::entity parent)
        : m_entity(scene.uuidOf(e)), m_toParent(parent == entt::null ? 0 : scene.uuidOf(parent)),
          m_label("Reparent " + entityName(scene, e))
    {
    }

    bool undo(EditContext& context) override { return move(context, m_fromParent, m_fromIndex, false); }
    bool redo(EditContext& context) override
    {
        return move(context, m_toParent, m_toIndex, m_toIndex == SIZE_MAX);
    }
    std::string name() const override { return m_label; }
};

class GroupCommand final : public EditCommand
{
private:
    std::string m_label;
    std::vector<std::unique_ptr<EditCommand>> m_commands;

public:
    GroupCommand(std::string label, std::vector<std::unique_ptr<EditCommand>> commands)
        : m_label(std::move(label)), m_commands(std::move(commands))
    {
    }

    bool undo(EditContext& context) override
    {
        bool any = false;
        for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
            any = (*it)->undo(context) || any;
        return any;
    }
    bool redo(EditContext& context) override
    {
        bool any = false;
        for (auto& command : m_commands)
            any = command->redo(context) || any;
        return any;
    }
    std::string name() const override { return m_label; }
};
}  // namespace

std::unique_ptr<EditCommand> setFieldCommand(Scene& scene, entt::entity e, const ComponentInfo& info,
                                             const FieldInfo& field, const FieldValue& before,
                                             const FieldValue& after)
{
    return std::make_unique<SetFieldCommand>(scene, e, info, field, before, after);
}

std::unique_ptr<EditCommand> transformCommand(EditContext& context, entt::entity e, const Transform& before,
                                              const Transform& after)
{
    const ComponentInfo* info = context.components.find("Transform");
    if (!info)
        return nullptr;
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (const FieldInfo& field : info->fields) {
        if (!field.saved || !field.set)
            continue;
        const FieldValue from = field.get(&before);
        const FieldValue to = field.get(&after);
        if (from != to)
            commands.push_back(setFieldCommand(context.scene, e, *info, field, from, to));
    }
    if (commands.empty())
        return nullptr;
    return groupCommand("Transform " + entityName(context.scene, e), std::move(commands));
}

std::unique_ptr<EditCommand> addComponentCommand(Scene& scene, entt::entity e, const ComponentInfo& info)
{
    return std::make_unique<ComponentPresenceCommand>(scene, e, info, true);
}

std::unique_ptr<EditCommand> removeComponentCommand(Scene& scene, entt::entity e, const ComponentInfo& info)
{
    return std::make_unique<ComponentPresenceCommand>(scene, e, info, false);
}

std::unique_ptr<EditCommand> createdEntityCommand(Scene& scene, entt::entity e)
{
    return std::make_unique<EntityPresenceCommand>(scene, e, true);
}

std::unique_ptr<EditCommand> destroyEntityCommand(Scene& scene, entt::entity e)
{
    return std::make_unique<EntityPresenceCommand>(scene, e, false);
}

std::unique_ptr<EditCommand> reparentCommand(Scene& scene, entt::entity e, entt::entity parent)
{
    return std::make_unique<ReparentCommand>(scene, e, parent);
}

std::unique_ptr<EditCommand> groupCommand(std::string name, std::vector<std::unique_ptr<EditCommand>> commands)
{
    return std::make_unique<GroupCommand>(std::move(name), std::move(commands));
}
