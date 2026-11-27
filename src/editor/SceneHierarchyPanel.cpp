#include "SceneHierarchyPanel.h"

#include "editor/EditCommands.h"

#include "engine/scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>

static constexpr const char* EntityPayload = "SCENE_ENTITY";

void SceneHierarchyPanel::draw(Scene& scene, const ComponentRegistry& components, EditHistory& history,
                               entt::entity& selected)
{
    m_selected = &selected;
    if (*m_selected != entt::null && !scene.registry().valid(*m_selected))
        *m_selected = entt::null;

    // Opens the tree down to a selection made elsewhere (e.g. picked in the Scene view)
    const bool reveal = *m_selected != m_revealed && *m_selected != entt::null;
    m_revealed = *m_selected;
    m_reveal = reveal;

    if (ImGui::Begin("Scene Hierarchy")) {
        scene.forEachChild(scene.root(), [&](entt::entity e) { drawNode(scene, e); });

        // Empty space below the tree: drop here to move to the top level
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(avail.x, std::max(avail.y, 20.0f)));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EntityPayload)) {
                m_reparentChild = *static_cast<const entt::entity*>(payload->Data);
                m_reparentTarget = scene.root();
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemClicked())
            *m_selected = entt::null;

        if (ImGui::BeginPopupContextWindow("HierarchyContext",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Create Empty")) {
                m_createRequested = true;
                m_createParent = entt::null;
            }
            ImGui::EndPopup();
        }

        if (*m_selected != entt::null && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete))
            m_destroyTarget = *m_selected;
    }
    ImGui::End();

    applyPendingEdits(scene, components, history);
}

void SceneHierarchyPanel::drawNode(Scene& scene, entt::entity e)
{
    const entt::registry& registry = scene.registry();
    const bool hasChildren = registry.get<Relationship>(e).childCount > 0;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (e == *m_selected)
        flags |= ImGuiTreeNodeFlags_Selected;

    const Name* name = registry.try_get<Name>(e);
    const char* label = name ? name->value.c_str() : "Entity";
    const void* id = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(entt::to_integral(e)));
    if (m_reveal && hasChildren && scene.isAncestor(e, *m_selected))
        ImGui::SetNextItemOpen(true);
    const bool open = ImGui::TreeNodeEx(id, flags, "%s", label);
    if (m_reveal && e == *m_selected)
        ImGui::SetScrollHereY();

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        *m_selected = e;
        m_revealed = e;  // picked here, no need to scroll to it
    }

    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(EntityPayload, &e, sizeof(e));
        ImGui::TextUnformatted(label);
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EntityPayload)) {
            m_reparentChild = *static_cast<const entt::entity*>(payload->Data);
            m_reparentTarget = e;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        *m_selected = e;
        m_revealed = e;
        if (ImGui::MenuItem("Create Child")) {
            m_createRequested = true;
            m_createParent = e;
        }
        if (ImGui::MenuItem("Delete"))
            m_destroyTarget = e;
        ImGui::EndPopup();
    }

    if (open && hasChildren) {
        scene.forEachChild(e, [&](entt::entity child) { drawNode(scene, child); });
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::applyPendingEdits(Scene& scene, const ComponentRegistry& components, EditHistory& history)
{
    EditContext context{scene, components};
    if (m_reparentChild != entt::null) {
        const entt::entity parent = m_reparentTarget == scene.root() ? entt::null : m_reparentTarget;
        history.execute(context, reparentCommand(scene, m_reparentChild, parent));
        m_reparentChild = entt::null;
        m_reparentTarget = entt::null;
    }
    if (m_createRequested) {
        *m_selected = scene.createGameObject("GameObject", m_createParent);
        history.push(createdEntityCommand(scene, *m_selected));
        m_createRequested = false;
        m_createParent = entt::null;
    }
    if (m_destroyTarget != entt::null) {
        history.execute(context, destroyEntityCommand(scene, m_destroyTarget));
        if (!scene.registry().valid(*m_selected))
            *m_selected = entt::null;
        m_destroyTarget = entt::null;
    }
}
