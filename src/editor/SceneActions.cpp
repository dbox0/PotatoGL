#include "SceneActions.h"

#include "editor/EditCommands.h"
#include "editor/EditHistory.h"
#include "editor/Selection.h"

#include "engine/scene/Scene.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/scene/Transform.h"

#include <imgui.h>

#include <charconv>
#include <memory>
#include <string_view>
#include <utility>

namespace
{
struct KindInfo
{
    CreateKind kind;
    const char* name;
    const char* mesh;  // builtin mesh, or nullptr
};

constexpr KindInfo Kinds[] = {
    {CreateKind::Empty, "GameObject", nullptr},
    {CreateKind::Cube, "Cube", "builtin:cube"},
    {CreateKind::Sphere, "Sphere", "builtin:sphere"},
    {CreateKind::Capsule, "Capsule", "builtin:capsule"},
    {CreateKind::Cylinder, "Cylinder", "builtin:cylinder"},
    {CreateKind::Plane, "Plane", "builtin:plane"},
    {CreateKind::Quad, "Quad", "builtin:quad"},
    {CreateKind::DirectionalLight, "Directional Light", nullptr},
    {CreateKind::PointLight, "Point Light", nullptr},
    {CreateKind::SpotLight, "Spot Light", nullptr},
    {CreateKind::Camera, "Camera", nullptr},
};

const KindInfo& kindInfo(CreateKind kind)
{
    for (const KindInfo& info : Kinds) {
        if (info.kind == kind)
            return info;
    }
    return Kinds[0];
}

// "Cube (3)" -> {"Cube", 3}; "Cube" -> {"Cube", 0}
std::pair<std::string_view, int> splitCopyNumber(std::string_view name)
{
    const std::size_t open = name.rfind(" (");
    if (open == std::string_view::npos || name.size() < open + 4 || name.back() != ')')
        return {name, 0};
    const std::string_view digits = name.substr(open + 2, name.size() - open - 3);
    int number = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (ec != std::errc() || end != digits.data() + digits.size() || number <= 0)
        return {name, 0};
    return {name.substr(0, open), number};
}

// Unity's duplicate naming: the next free "Name (n)" among the siblings
std::string copyName(const Scene& scene, entt::entity parent, std::string_view name)
{
    const std::string_view stem = splitCopyNumber(name).first;
    int highest = 0;
    scene.forEachChild(parent, [&](entt::entity sibling) {
        const Name* siblingName = scene.registry().try_get<Name>(sibling);
        if (!siblingName)
            return;
        const auto [siblingStem, number] = splitCopyNumber(siblingName->value);
        if (siblingStem == stem)
            highest = std::max(highest, number);
    });
    return std::string(stem) + " (" + std::to_string(highest + 1) + ")";
}

std::string groupName(const char* verb, Scene& scene, const std::vector<entt::entity>& entities)
{
    if (entities.size() == 1) {
        const Name* name = scene.registry().try_get<Name>(entities.front());
        return std::string(verb) + " " + (name && !name->value.empty() ? name->value : std::string("Object"));
    }
    return std::string(verb) + " " + std::to_string(entities.size()) + " Objects";
}

void pushCreated(EditContext& context, EditHistory& history, const char* verb,
                 const std::vector<entt::entity>& created)
{
    if (created.empty())
        return;
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (const entt::entity e : created)
        commands.push_back(createdEntityCommand(context.scene, e));
    history.push(groupCommand(groupName(verb, context.scene, created), std::move(commands)));
}
}  // namespace

std::vector<entt::entity> selectionRoots(const EditContext& context, const Selection& selection)
{
    const Scene& scene = context.scene;
    std::vector<entt::entity> roots;
    for (const entt::entity e : selection.items()) {
        if (!scene.registry().valid(e))
            continue;
        bool nested = false;
        for (const entt::entity other : selection.items()) {
            if (other != e && scene.registry().valid(other) && scene.isAncestor(other, e)) {
                nested = true;
                break;
            }
        }
        if (!nested)
            roots.push_back(e);
    }
    return roots;
}

void SceneActions::drawCreateItems(entt::entity parent)
{
    auto item = [&](CreateKind kind, const char* label = nullptr) {
        if (ImGui::MenuItem(label ? label : kindInfo(kind).name))
            create(kind, parent);
    };
    item(CreateKind::Empty, "Create Empty");
    if (ImGui::BeginMenu("3D Object")) {
        for (const CreateKind kind : {CreateKind::Cube, CreateKind::Sphere, CreateKind::Capsule, CreateKind::Cylinder,
                                      CreateKind::Plane, CreateKind::Quad})
            item(kind);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Light")) {
        for (const CreateKind kind : {CreateKind::DirectionalLight, CreateKind::PointLight, CreateKind::SpotLight})
            item(kind);
        ImGui::EndMenu();
    }
    item(CreateKind::Camera);
}

void SceneActions::drawEditItems(bool hasSelection)
{
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection))
        duplicate();
    if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection))
        copy();
    if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste()))
        paste();
    if (ImGui::MenuItem("Delete", "Del", false, hasSelection))
        remove();
}

void SceneActions::apply(EditContext& context, EditHistory& history, Selection& selection, const glm::vec3& spawn)
{
    selection.prune(context.scene.registry());
    for (const Request& request : m_requests) {
        switch (request.type) {
        case Type::Create:
            applyCreate(context, history, selection, request, spawn);
            break;
        case Type::Duplicate:
            applyDuplicate(context, history, selection);
            break;
        case Type::Copy:
            applyCopy(context, selection);
            break;
        case Type::Paste:
            applyPaste(context, history, selection);
            break;
        case Type::Delete:
            applyDelete(context, history, selection);
            break;
        }
    }
    m_requests.clear();
}

void SceneActions::applyCreate(EditContext& context, EditHistory& history, Selection& selection,
                               const Request& request, const glm::vec3& spawn)
{
    Scene& scene = context.scene;
    entt::registry& registry = scene.registry();
    const entt::entity parent = request.parent != entt::null && registry.valid(request.parent) ? request.parent
                                                                                                : entt::null;
    const KindInfo& info = kindInfo(request.kind);
    const entt::entity e = scene.createGameObject(info.name, parent);
    Transform& transform = registry.get<Transform>(e);
    if (parent == entt::null)
        transform.setPos(spawn);
    if (info.mesh)
        registry.emplace<MeshRenderer>(e, info.mesh);

    // Lights and cameras look down their local -Z
    switch (request.kind) {
    case CreateKind::DirectionalLight:
        transform.setEulerAngles(glm::radians(glm::vec3(-50.0f, -30.0f, 0.0f)));
        registry.emplace<DirectionalLight>(e);
        break;
    case CreateKind::PointLight:
        registry.emplace<PointLight>(e);
        break;
    case CreateKind::SpotLight:
        transform.setEulerAngles(glm::radians(glm::vec3(-90.0f, 0.0f, 0.0f)));
        registry.emplace<SpotLight>(e);
        break;
    case CreateKind::Camera:
        registry.emplace<CameraComponent>(e);
        break;
    default:
        break;
    }

    history.push(createdEntityCommand(scene, e));
    selection.set(e);
}

void SceneActions::applyDuplicate(EditContext& context, EditHistory& history, Selection& selection)
{
    Scene& scene = context.scene;
    std::vector<entt::entity> created;
    for (const entt::entity original : selectionRoots(context, selection)) {
        const entt::entity parent = scene.parentOf(original);
        const std::string tree = saveEntityTree(scene, context.components, original);
        const Name* originalName = scene.registry().try_get<Name>(original);
        std::string name = copyName(scene, parent, originalName ? originalName->value : "GameObject");
        const entt::entity copy =
            loadEntityTree(scene, context.components, tree, parent, scene.siblingIndex(original) + 1, true);
        if (copy == entt::null)
            continue;
        scene.registry().emplace_or_replace<Name>(copy, std::move(name));
        created.push_back(copy);
    }
    pushCreated(context, history, "Duplicate", created);
    if (!created.empty())
        selection.assign(std::move(created));
}

void SceneActions::applyCopy(EditContext& context, const Selection& selection)
{
    const std::vector<entt::entity> roots = selectionRoots(context, selection);
    if (roots.empty())
        return;
    m_clipboard.clear();
    for (const entt::entity e : roots)
        m_clipboard.push_back({context.scene.uuidOf(context.scene.parentOf(e)),
                               saveEntityTree(context.scene, context.components, e)});
}

void SceneActions::applyPaste(EditContext& context, EditHistory& history, Selection& selection)
{
    Scene& scene = context.scene;
    std::vector<entt::entity> created;
    for (const Copied& copied : m_clipboard) {
        entt::entity parent = copied.parent == 0 ? entt::null : scene.findByUuid(copied.parent);
        if (parent != entt::null && !scene.registry().valid(parent))
            parent = entt::null;
        const entt::entity e = loadEntityTree(scene, context.components, copied.tree, parent, SIZE_MAX, true);
        if (e != entt::null)
            created.push_back(e);
    }
    pushCreated(context, history, "Paste", created);
    if (!created.empty())
        selection.assign(std::move(created));
}

void SceneActions::applyDelete(EditContext& context, EditHistory& history, Selection& selection)
{
    const std::vector<entt::entity> roots = selectionRoots(context, selection);
    if (roots.empty())
        return;
    const std::string name = groupName("Delete", context.scene, roots);
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (const entt::entity e : roots)
        commands.push_back(destroyEntityCommand(context.scene, e));
    history.execute(context, groupCommand(name, std::move(commands)));
    selection.clear();
}
