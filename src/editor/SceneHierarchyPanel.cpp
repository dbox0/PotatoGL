#include "SceneHierarchyPanel.h"

#include "editor/DragDrop.h"
#include "editor/EditCommands.h"
#include "editor/SceneActions.h"
#include "editor/Selection.h"

#include "engine/scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace
{
constexpr ImU32 PrefabColor = IM_COL32(110, 175, 255, 255);
constexpr ImU32 PrefabChildColor = IM_COL32(150, 195, 245, 255);

enum class Icon
{
    Empty,
    Mesh,
    Prefab,
    Camera,
    DirectionalLight,
    PointLight,
    SpotLight,
};

Icon iconOf(const entt::registry& registry, entt::entity e)
{
    if (registry.all_of<PrefabInstance>(e))
        return Icon::Prefab;
    if (registry.all_of<CameraComponent>(e))
        return Icon::Camera;
    if (registry.all_of<DirectionalLight>(e))
        return Icon::DirectionalLight;
    if (registry.all_of<PointLight>(e))
        return Icon::PointLight;
    if (registry.all_of<SpotLight>(e))
        return Icon::SpotLight;
    if (registry.all_of<MeshRenderer>(e))
        return Icon::Mesh;
    return Icon::Empty;
}

// An isometric cube: outlined for an empty object, shaded faces otherwise
void drawCube(ImDrawList* dl, ImVec2 c, float r, bool filled, ImU32 top, ImU32 left, ImU32 right, ImU32 line)
{
    const ImVec2 t(c.x, c.y - r), ur(c.x + r * 0.866f, c.y - r * 0.5f), lr(c.x + r * 0.866f, c.y + r * 0.5f),
        b(c.x, c.y + r), ll(c.x - r * 0.866f, c.y + r * 0.5f), ul(c.x - r * 0.866f, c.y - r * 0.5f);
    if (filled) {
        const ImVec2 topFace[] = {t, ur, c, ul};
        const ImVec2 leftFace[] = {ul, c, b, ll};
        const ImVec2 rightFace[] = {c, ur, lr, b};
        dl->AddConvexPolyFilled(topFace, 4, top);
        dl->AddConvexPolyFilled(leftFace, 4, left);
        dl->AddConvexPolyFilled(rightFace, 4, right);
        return;
    }
    const ImVec2 outline[] = {t, ur, lr, b, ll, ul};
    dl->AddPolyline(outline, 6, line, ImDrawFlags_Closed, 1.0f);
    dl->AddLine(c, ul, line);
    dl->AddLine(c, ur, line);
    dl->AddLine(c, b, line);
}

// In the square at min with side s
void drawIcon(ImDrawList* dl, ImVec2 min, float s, Icon icon)
{
    const ImVec2 c(min.x + s * 0.5f, min.y + s * 0.5f);
    const float r = s * 0.42f;
    constexpr ImU32 Yellow = IM_COL32(250, 210, 90, 255);
    switch (icon) {
    case Icon::Empty:
        drawCube(dl, c, r, false, 0, 0, 0, IM_COL32(150, 150, 155, 255));
        break;
    case Icon::Mesh:
        drawCube(dl, c, r, true, IM_COL32(215, 215, 220, 255), IM_COL32(160, 160, 168, 255),
                 IM_COL32(120, 120, 128, 255), 0);
        break;
    case Icon::Prefab:
        drawCube(dl, c, r, true, IM_COL32(150, 200, 255, 255), IM_COL32(80, 150, 235, 255),
                 IM_COL32(50, 110, 200, 255), 0);
        break;
    case Icon::Camera: {
        constexpr ImU32 Gray = IM_COL32(200, 200, 205, 255);
        dl->AddRectFilled(ImVec2(c.x - r, c.y - r * 0.5f), ImVec2(c.x + r * 0.3f, c.y + r * 0.6f), Gray, 1.5f);
        dl->AddTriangleFilled(ImVec2(c.x + r * 0.3f, c.y + r * 0.05f), ImVec2(c.x + r, c.y - r * 0.45f),
                              ImVec2(c.x + r, c.y + r * 0.55f), Gray);
        break;
    }
    case Icon::DirectionalLight:
        dl->AddCircleFilled(c, r * 0.45f, Yellow);
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * 0.785398f;
            const ImVec2 d(std::cos(a), std::sin(a));
            dl->AddLine(ImVec2(c.x + d.x * r * 0.65f, c.y + d.y * r * 0.65f), ImVec2(c.x + d.x * r, c.y + d.y * r),
                        Yellow, 1.2f);
        }
        break;
    case Icon::PointLight:
        dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.2f), r * 0.62f, Yellow);
        dl->AddRectFilled(ImVec2(c.x - r * 0.28f, c.y + r * 0.45f), ImVec2(c.x + r * 0.28f, c.y + r),
                          IM_COL32(170, 170, 175, 255), 1.0f);
        break;
    case Icon::SpotLight:
        dl->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r * 0.85f, c.y + r * 0.7f),
                              ImVec2(c.x - r * 0.85f, c.y + r * 0.7f), IM_COL32(250, 210, 90, 140));
        dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.7f), r * 0.35f, Yellow);
        break;
    }
}
}  // namespace

void SceneHierarchyPanel::draw(Scene& scene, const ComponentRegistry& components, EditHistory& history,
                               Selection& selection, SceneActions& actions)
{
    m_selection = &selection;
    m_actions = &actions;
    selection.prune(scene.registry());

    // Opens the tree down to a selection made elsewhere (e.g. picked in the Scene view)
    const entt::entity active = selection.active();
    m_reveal = active != m_revealed && active != entt::null;
    m_revealed = active;

    m_focused = false;
    if (ImGui::Begin("Scene Hierarchy")) {
        m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        m_drawnRows.clear();
        scene.forEachChild(scene.root(), [&](entt::entity e) { drawNode(scene, e, false); });
        m_rows.swap(m_drawnRows);

        // Empty space below the tree: drop here to move to the top level
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(avail.x, std::max(avail.y, 20.0f)));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EntityPayload)) {
                m_reparentChild = *static_cast<const entt::entity*>(payload->Data);
                m_reparentTarget = scene.root();
            }
            if (const char* asset = acceptAssetDrop({AssetKind::Model, AssetKind::Prefab}))
                actions.instantiate(asset, entt::null, SceneActions::Placement::Keep);
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemClicked() && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
            selection.clear();

        if (ImGui::BeginPopupContextWindow("HierarchyContext",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            actions.drawCreateItems();
            ImGui::Separator();
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, actions.canPaste()))
                actions.paste();
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        m_clickedSelected = entt::null;

    applyPendingEdits(scene, components, history);
}

void SceneHierarchyPanel::click(entt::entity e)
{
    Selection& selection = *m_selection;
    const ImGuiIO& io = ImGui::GetIO();
    const auto anchor = std::find(m_rows.begin(), m_rows.end(), m_anchor);
    const auto target = std::find(m_rows.begin(), m_rows.end(), e);

    if (io.KeyShift && anchor != m_rows.end() && target != m_rows.end()) {
        // Anchor to e in either direction, so e ends up last (active)
        std::vector<entt::entity> range;
        const auto step = anchor < target ? 1 : -1;
        for (auto it = anchor;; it += step) {
            range.push_back(*it);
            if (it == target)
                break;
        }
        if (io.KeyCtrl) {
            for (const entt::entity r : range)
                selection.add(r);
        } else {
            selection.assign(std::move(range));
        }
    } else if (io.KeyCtrl) {
        selection.toggle(e);
        m_anchor = e;
    } else if (selection.contains(e) && selection.size() > 1) {
        // Could be the start of dragging the whole selection; narrowed on release
        selection.add(e);
        m_clickedSelected = e;
        m_anchor = e;
    } else {
        selection.set(e);
        m_anchor = e;
    }
    m_revealed = selection.active();  // picked here, no need to scroll to it
}

void SceneHierarchyPanel::drawNode(Scene& scene, entt::entity e, bool inPrefab)
{
    const entt::registry& registry = scene.registry();
    Selection& selection = *m_selection;
    const bool hasChildren = registry.get<Relationship>(e).childCount > 0;
    m_drawnRows.push_back(e);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selection.contains(e))
        flags |= ImGuiTreeNodeFlags_Selected;

    const Name* name = registry.try_get<Name>(e);
    const char* label = name ? name->value.c_str() : "Entity";
    const void* id = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(entt::to_integral(e)));
    if (m_reveal && hasChildren && scene.isAncestor(e, selection.active()))
        ImGui::SetNextItemOpen(true);
    // The label is drawn after the node, behind an icon
    const bool open = ImGui::TreeNodeEx(id, flags, "%s", "");
    {
        const Icon icon = iconOf(registry, e);
        const bool prefabRoot = icon == Icon::Prefab;
        inPrefab = inPrefab || prefabRoot;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        const float size = ImGui::GetFontSize();
        const float x = rowMin.x + ImGui::GetTreeNodeToLabelSpacing();
        const float y = rowMin.y + (ImGui::GetItemRectSize().y - size) * 0.5f;
        drawIcon(dl, ImVec2(x, y), size, icon);
        const ImU32 color = prefabRoot ? PrefabColor : inPrefab ? PrefabChildColor : ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddText(ImVec2(x + size + ImGui::GetStyle().ItemInnerSpacing.x, y), color, label);
    }
    if (m_reveal && e == selection.active())
        ImGui::SetScrollHereY();

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        click(e);
    if (m_clickedSelected == e && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::IsItemHovered()) {
        const float threshold = ImGui::GetIO().MouseDragThreshold;
        if (ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < threshold * threshold)
            selection.set(e);
        m_clickedSelected = entt::null;
    }

    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(EntityPayload, &e, sizeof(e));
        if (selection.contains(e) && selection.size() > 1)
            ImGui::Text("%zu objects", selection.size());
        else
            ImGui::TextUnformatted(label);
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EntityPayload)) {
            m_reparentChild = *static_cast<const entt::entity*>(payload->Data);
            m_reparentTarget = e;
        }
        if (const char* asset = acceptAssetDrop({AssetKind::Model, AssetKind::Prefab}))
            m_actions->instantiate(asset, e, SceneActions::Placement::Keep);
        if (registry.all_of<MeshRenderer>(e)) {
            if (const char* material = acceptAssetDrop({AssetKind::Material}))
                m_actions->assignMaterial(e, material);
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (!selection.contains(e)) {
            selection.set(e);
            m_anchor = e;
        }
        m_revealed = selection.active();
        if (ImGui::BeginMenu("Create Child")) {
            m_actions->drawCreateItems(e);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        m_actions->drawEditItems(true);
        ImGui::EndPopup();
    }

    if (open && hasChildren) {
        scene.forEachChild(e, [&](entt::entity child) { drawNode(scene, child, inPrefab); });
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::applyPendingEdits(Scene& scene, const ComponentRegistry& components, EditHistory& history)
{
    if (m_reparentChild == entt::null)
        return;
    EditContext context{scene, components};
    const entt::entity parent = m_reparentTarget == scene.root() ? entt::null : m_reparentTarget;

    // Dragging a selected object takes the rest of the selection along
    std::vector<entt::entity> moved{m_reparentChild};
    if (m_selection->contains(m_reparentChild))
        moved = selectionRoots(context, *m_selection);
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (const entt::entity e : moved) {
        if (scene.registry().valid(e))
            commands.push_back(reparentCommand(scene, e, parent));
    }
    if (commands.size() == 1)
        history.execute(context, std::move(commands.front()));
    else if (!commands.empty())
        history.execute(context,
                        groupCommand("Reparent " + std::to_string(commands.size()) + " Objects", std::move(commands)));
    m_reparentChild = entt::null;
    m_reparentTarget = entt::null;
}
