#include "SceneHierarchyPanel.h"

#include "engine/scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>

static constexpr const char* EntityPayload = "SCENE_ENTITY";

void SceneHierarchyPanel::draw(Scene& scene)
{
    if (m_selected != entt::null && !scene.registry().valid(m_selected))
        m_selected = entt::null;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(m_width, viewport->WorkSize.y), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("Scene Hierarchy", nullptr, flags)) {
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
            m_selected = entt::null;

        if (ImGui::BeginPopupContextWindow("HierarchyContext",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Create Empty")) {
                m_createRequested = true;
                m_createParent = entt::null;
            }
            ImGui::EndPopup();
        }

        if (m_selected != entt::null && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete))
            m_destroyTarget = m_selected;
    }
    ImGui::End();

    applyPendingEdits(scene);
}

void SceneHierarchyPanel::drawNode(Scene& scene, entt::entity e)
{
    const entt::registry& registry = scene.registry();
    const bool hasChildren = registry.get<Relationship>(e).childCount > 0;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (e == m_selected)
        flags |= ImGuiTreeNodeFlags_Selected;

    const Name* name = registry.try_get<Name>(e);
    const char* label = name ? name->value.c_str() : "Entity";
    const void* id = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(entt::to_integral(e)));
    const bool open = ImGui::TreeNodeEx(id, flags, "%s", label);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        m_selected = e;

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
        m_selected = e;
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

void SceneHierarchyPanel::applyPendingEdits(Scene& scene)
{
    if (m_reparentChild != entt::null) {
        scene.setParent(m_reparentChild, m_reparentTarget);
        m_reparentChild = entt::null;
        m_reparentTarget = entt::null;
    }
    if (m_createRequested) {
        m_selected = scene.createGameObject("GameObject", m_createParent);
        m_createRequested = false;
        m_createParent = entt::null;
    }
    if (m_destroyTarget != entt::null) {
        scene.destroyRecursive(m_destroyTarget);
        if (!scene.registry().valid(m_selected))
            m_selected = entt::null;
        m_destroyTarget = entt::null;
    }
}
