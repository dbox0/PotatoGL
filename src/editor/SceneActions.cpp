#include "SceneActions.h"

#include "editor/EditCommands.h"
#include "editor/EditHistory.h"
#include "editor/Selection.h"

#include "editor/AssetOps.h"

#include "engine/assets/AssetCache.h"
#include "engine/assets/AssetTypes.h"
#include "engine/reflect/ComponentRegistry.h"
#include "engine/scene/Prefab.h"
#include "engine/scene/Scene.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/scene/Transform.h"
#include "engine/ui/UiComponents.h"
#include "engine/ui/UiLayout.h"

#include <imgui.h>

#include <charconv>
#include <filesystem>
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
    {CreateKind::UICanvas, "Canvas", nullptr},
    {CreateKind::UIPanel, "Panel", nullptr},
    {CreateKind::UIShape, "Shape", nullptr},
    {CreateKind::UIImage, "Image", nullptr},
};

bool isUiKind(CreateKind kind)
{
    return kind == CreateKind::UICanvas || kind == CreateKind::UIPanel || kind == CreateKind::UIShape ||
           kind == CreateKind::UIImage;
}

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

// A world position in parent's space; parent = entt::null: the top level
glm::vec3 localPoint(const entt::registry& registry, entt::entity parent, const glm::vec3& world)
{
    if (parent == entt::null)
        return world;
    const WorldTransform* parentWorld = registry.try_get<WorldTransform>(parent);
    return parentWorld ? glm::vec3(glm::inverse(parentWorld->matrix) * glm::vec4(world, 1.0f)) : world;
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

entt::entity SceneActions::parentFor(const EditContext& context, entt::entity requested) const
{
    const entt::registry& registry = context.scene.registry();
    if (requested != entt::null && registry.valid(requested))
        return requested;
    return m_topParent != entt::null && registry.valid(m_topParent) ? m_topParent : entt::null;
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
    if (ImGui::BeginMenu("UI")) {
        for (const CreateKind kind : {CreateKind::UICanvas, CreateKind::UIPanel, CreateKind::UIShape, CreateKind::UIImage})
            item(kind);
        ImGui::EndMenu();
    }
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
        case Type::Instantiate:
            applyInstantiate(context, history, selection, request, spawn);
            break;
        case Type::AssignMaterial:
            applyAssignMaterial(context, history, request);
            break;
        }
    }
    m_requests.clear();
}

// Under the requested parent, else the selection, if that is or is under a Canvas; otherwise
// a new Canvas is made first. The whole new subtree is one undo step.
void SceneActions::applyCreateUi(EditContext& context, EditHistory& history, Selection& selection,
                                 const Request& request)
{
    Scene& scene = context.scene;
    entt::registry& registry = scene.registry();
    const KindInfo& info = kindInfo(request.kind);
    entt::entity created = entt::null;
    entt::entity parent = entt::null;
    if (request.kind != CreateKind::UICanvas) {
        entt::entity anchor = request.parent != entt::null && registry.valid(request.parent) ? request.parent
                                                                                              : selection.active();
        if (anchor != entt::null && registry.valid(anchor) && uiCanvasOf(scene, anchor) != entt::null &&
            (registry.all_of<Canvas>(anchor) || registry.all_of<RectTransform>(anchor)))
            parent = anchor;
    }
    if (parent == entt::null) {
        const entt::entity top = request.kind == CreateKind::UICanvas ? parentFor(context, request.parent)
                                                                      : parentFor(context, entt::null);
        parent = scene.createGameObject("Canvas", top);
        registry.emplace<Canvas>(parent);
        created = parent;
        if (request.kind == CreateKind::UICanvas) {
            history.push(createdEntityCommand(scene, parent));
            selection.set(parent);
            return;
        }
    }

    const entt::entity e = scene.createGameObject(info.name, parent);
    RectTransform& rt = registry.emplace<RectTransform>(e);
    switch (request.kind) {
    case CreateKind::UIPanel:
        rt.anchorMin = glm::vec2(0.0f);
        rt.anchorMax = glm::vec2(1.0f);
        rt.sizeDelta = glm::vec2(0.0f);
        registry.emplace<UIImage>(e).color = glm::vec4(1.0f, 1.0f, 1.0f, 0.39f);
        break;
    case CreateKind::UIShape:
        registry.emplace<UIShape>(e).shape = UIShapeType::RoundedRect;
        break;
    case CreateKind::UIImage:
        registry.emplace<UIImage>(e);
        break;
    default:
        break;
    }
    history.push(createdEntityCommand(scene, created != entt::null ? created : e));
    selection.set(e);
}

void SceneActions::applyCreate(EditContext& context, EditHistory& history, Selection& selection,
                               const Request& request, const glm::vec3& spawn)
{
    if (isUiKind(request.kind)) {
        applyCreateUi(context, history, selection, request);
        return;
    }
    Scene& scene = context.scene;
    entt::registry& registry = scene.registry();
    const bool topLevel = request.parent == entt::null || !registry.valid(request.parent);
    const entt::entity parent = parentFor(context, request.parent);
    const KindInfo& info = kindInfo(request.kind);
    const entt::entity e = scene.createGameObject(info.name, parent);
    Transform& transform = registry.get<Transform>(e);
    if (topLevel)
        transform.setPos(localPoint(registry, parent, spawn));
    if (info.mesh)
        registry.emplace<MeshRenderer>(e).mesh = info.mesh;

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
        parent = parentFor(context, parent);
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

void SceneActions::applyInstantiate(EditContext& context, EditHistory& history, Selection& selection,
                                    const Request& request, const glm::vec3& spawn)
{
    Scene& scene = context.scene;
    entt::registry& registry = scene.registry();
    const bool topLevel = request.parent == entt::null || !registry.valid(request.parent);
    const entt::entity parent = parentFor(context, request.parent);
    const std::string name = std::filesystem::path(request.asset).stem().string();
    entt::entity e = entt::null;
    switch (assetKindOf(request.asset)) {
    case AssetKind::Model:
        e = scene.createGameObject(name, parent);
        if (m_assets && m_assets->skeleton(request.asset)) {
            registry.emplace<SkinnedMeshRenderer>(e).mesh = request.asset;
            const std::vector<std::string> clips = m_assets->clipRefs(request.asset);
            registry.emplace<Animator>(e).clip = clips.empty() ? std::string() : clips.front();
        } else {
            registry.emplace<MeshRenderer>(e).mesh = request.asset;
        }
        break;
    case AssetKind::Prefab:
        if (request.asset != m_openPrefab)
            e = instantiatePrefab(scene, context.components, request.asset, parent);
        break;
    default:
        break;
    }
    if (e == entt::null)
        return;

    Transform& transform = registry.get<Transform>(e);
    if (request.placement == Placement::Spawn && topLevel)
        transform.setPos(localPoint(registry, parent, spawn));
    else if (request.placement == Placement::At)
        transform.setPos(localPoint(registry, parent, request.position));
    else if (parent != entt::null)
        transform.setPos(glm::vec3(0.0f));
    history.push(createdEntityCommand(scene, e));
    selection.set(e);
}

void SceneActions::applyAssignMaterial(EditContext& context, EditHistory& history, const Request& request)
{
    Scene& scene = context.scene;
    const entt::entity e = request.parent;
    if (!scene.registry().valid(e))
        return;
    const ComponentInfo* info = context.components.find("MeshRenderer");
    if (info && !info->has(scene.registry(), e))
        info = context.components.find("SkinnedMeshRenderer");
    if (!info || !info->has(scene.registry(), e))
        return;
    void* data = info->get(scene.registry(), e);
    for (const FieldInfo& field : info->fields) {
        if (field.key != "material" || !field.set)
            continue;
        const FieldValue before = field.get(data);
        const FieldValue after = request.asset;
        if (before == after)
            return;
        field.set(data, after);
        history.push(setFieldCommand(scene, e, *info, field, before, after));
        return;
    }
}
