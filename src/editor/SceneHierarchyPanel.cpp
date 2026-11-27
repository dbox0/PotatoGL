#include "SceneHierarchyPanel.h"

#include "editor/DragDrop.h"
#include "editor/EditCommands.h"
#include "editor/SceneActions.h"
#include "editor/Selection.h"

#include "engine/scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

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
        scene.forEachChild(scene.root(), [&](entt::entity e) { drawNode(scene, e); });
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

void SceneHierarchyPanel::drawNode(Scene& scene, entt::entity e)
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
    const bool open = ImGui::TreeNodeEx(id, flags, "%s", label);
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
        scene.forEachChild(e, [&](entt::entity child) { drawNode(scene, child); });
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
