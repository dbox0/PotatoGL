#include "SceneViewPanel.h"

#include "editor/EditCommands.h"

#include "engine/app/CameraController.h"
#include "engine/app/Overlay.h"
#include "engine/render/Renderer.h"
#include "engine/scene/Camera.h"
#include "engine/scene/Scene.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <imgui.h>
#include <ImGuizmo.h>

static constexpr glm::vec4 OutlineColor(1.0f, 0.25f, 0.0f, 1.0f);  // linear

void SceneViewPanel::draw(const OverlayContext& context, EditHistory& history, entt::entity& selected)
{
    m_view.draw(nullptr, [&] {
        // No widget over the image: ImGuizmo only grabs the mouse when no item is hovered
        const bool clicked = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                             !ImGui::IsAnyItemHovered();
        const ImVec2 mouse = ImGui::GetMousePos();

        drawToolbar();
        drawGizmo(context, history, selected);
        handleHotkeys(context, history, selected);

        if (clicked && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
            m_pickX = static_cast<int>((mouse.x - m_view.imageX()) * scale.x);
            m_pickY = m_view.target().height() - 1 - static_cast<int>((mouse.y - m_view.imageY()) * scale.y);
            m_pickRequested = true;
        }
    });
}

void SceneViewPanel::drawToolbar()
{
    struct Button
    {
        const char* label;
        Tool tool;
    };
    static constexpr Button buttons[] = {
        {"Q", Tool::None}, {"W Move", Tool::Move}, {"E Rotate", Tool::Rotate}, {"R Scale", Tool::Scale}};

    ImGui::SetCursorScreenPos(ImVec2(m_view.imageX() + 8.0f, m_view.imageY() + 8.0f));
    for (const Button& b : buttons) {
        const bool active = m_tool == b.tool;
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(b.label))
            m_tool = b.tool;
        if (active)
            ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    if (ImGui::SmallButton(m_local ? "X Local" : "X Global"))
        m_local = !m_local;
}

void SceneViewPanel::handleHotkeys(const OverlayContext& context, EditHistory& history, entt::entity& selected)
{
    if (!(m_view.hovered() || m_view.focused()) || ImGui::GetIO().WantTextInput || context.controller.isLooking() ||
        ImGuizmo::IsUsing())
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
        m_tool = Tool::None;
    if (ImGui::IsKeyPressed(ImGuiKey_W, false))
        m_tool = Tool::Move;
    if (ImGui::IsKeyPressed(ImGuiKey_E, false))
        m_tool = Tool::Rotate;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false))
        m_tool = Tool::Scale;
    if (ImGui::IsKeyPressed(ImGuiKey_X, false))
        m_local = !m_local;
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && selected != entt::null &&
        context.scene.registry().valid(selected)) {
        EditContext edit{context.scene, context.components};
        history.execute(edit, destroyEntityCommand(context.scene, selected));
        selected = entt::null;
    }
}

void SceneViewPanel::drawGizmo(const OverlayContext& context, EditHistory& history, entt::entity selected)
{
    entt::registry& registry = context.scene.registry();
    const bool editable = m_tool != Tool::None && selected != entt::null && registry.valid(selected) &&
                          registry.all_of<Transform, WorldTransform>(selected);

    // Drag ended (or its object went away): one complete edit
    if (m_dragStart && (!ImGuizmo::IsUsing() || !editable || selected != m_dragEntity)) {
        if (registry.valid(m_dragEntity) && registry.all_of<Transform>(m_dragEntity)) {
            EditContext edit{context.scene, context.components};
            history.push(transformCommand(edit, m_dragEntity, *m_dragStart, registry.get<Transform>(m_dragEntity)));
        }
        m_dragStart.reset();
        m_dragEntity = entt::null;
    }
    if (!editable)
        return;

    Camera& camera = context.camera;
    camera.setAspect(m_view.aspect());
    const glm::mat4 view = camera.getView();
    const glm::mat4 projection = camera.getProjection();

    ImGuizmo::SetOrthographic(camera.isOrthographic());
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(m_view.imageX(), m_view.imageY(), m_view.imageWidth(), m_view.imageHeight());

    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    float snapValue = 0.5f;
    if (m_tool == Tool::Rotate) {
        operation = ImGuizmo::ROTATE;
        snapValue = 15.0f;
    } else if (m_tool == Tool::Scale) {
        operation = ImGuizmo::SCALE;
        snapValue = 0.1f;
    }
    const float snap[3] = {snapValue, snapValue, snapValue};
    const bool snapping = ImGui::GetIO().KeyCtrl;

    glm::mat4 world = registry.get<WorldTransform>(selected).matrix;
    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation,
                                              m_local ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(world),
                                              nullptr, snapping ? snap : nullptr);

    Transform& transform = registry.get<Transform>(selected);
    if (ImGuizmo::IsUsing() && !m_dragStart) {
        m_dragStart = transform;
        m_dragEntity = selected;
    }
    if (!changed)
        return;

    glm::mat4 parentWorld(1.0f);
    const entt::entity parent = registry.get<Relationship>(selected).parent;
    if (parent != entt::null) {
        if (const WorldTransform* w = registry.try_get<WorldTransform>(parent))
            parentWorld = w->matrix;
    }
    glm::vec3 scale, translation, skew;
    glm::vec4 perspective;
    glm::quat rotation;
    if (!glm::decompose(glm::inverse(parentWorld) * world, scale, rotation, translation, skew, perspective))
        return;
    transform.setPos(translation);
    transform.setRot(rotation);
    transform.setScale(scale);
    registry.get<WorldTransform>(selected).matrix = world;
}

void SceneViewPanel::render(const OverlayContext& context, entt::entity& selected)
{
    if (!m_view.visible()) {
        m_pickRequested = false;
        return;
    }
    const RenderTarget& target = m_view.target();
    Camera& camera = context.camera;
    camera.setAspect(m_view.aspect());
    const glm::mat4 viewProj = camera.getProjection() * camera.getView();
    context.renderer.render(viewProj, target);

    const entt::registry& registry = context.scene.registry();
    const bool hasSelection = selected != entt::null && registry.valid(selected);
    if (!hasSelection && !m_pickRequested)
        return;

    m_pick.resize(target.width(), target.height());
    context.renderer.renderIds(viewProj, m_pick);

    if (m_pickRequested) {
        m_pickRequested = false;
        const std::uint32_t id = m_pick.read(m_pickX, m_pickY);
        const auto e = static_cast<entt::entity>(id);
        selected = id != PickBuffer::NoId && registry.valid(e) ? e : entt::null;
    }
    if (hasSelection) {
        const int width = static_cast<int>(2.0f * ImGui::GetIO().DisplayFramebufferScale.x + 0.5f);
        context.renderer.drawOutline(target, m_pick, OutlineColor, width);
    }
}

void SceneViewPanel::release()
{
    m_view.release();
    m_pick.release();
}
