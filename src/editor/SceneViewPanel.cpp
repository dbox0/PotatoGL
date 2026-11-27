#include "SceneViewPanel.h"

#include "editor/DragDrop.h"
#include "editor/EditCommands.h"
#include "editor/SceneActions.h"
#include "editor/Selection.h"

#include "engine/app/CameraController.h"
#include "engine/app/Overlay.h"
#include "engine/render/Renderer.h"
#include "engine/scene/Camera.h"
#include "engine/scene/Scene.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <imgui.h>
#include <ImGuizmo.h>

#include <cmath>
#include <memory>
#include <string>

static constexpr glm::vec4 OutlineColor(1.0f, 0.25f, 0.0f, 1.0f);  // linear

static void applyPick(Selection& selection, entt::entity hit, bool additive)
{
    if (!additive)
        selection.set(hit);
    else if (hit != entt::null)
        selection.toggle(hit);
}

glm::vec3 SceneViewPanel::dropPoint(const OverlayContext& context, float x, float y) const
{
    const Camera& camera = context.camera;
    const glm::vec2 ndc((x - m_view.imageX()) / std::max(m_view.imageWidth(), 1.0f) * 2.0f - 1.0f,
                        1.0f - (y - m_view.imageY()) / std::max(m_view.imageHeight(), 1.0f) * 2.0f);
    const glm::mat4 inverse = glm::inverse(camera.getProjection() * camera.getView());
    const glm::vec4 nearH = inverse * glm::vec4(ndc, -1.0f, 1.0f);
    const glm::vec4 farH = inverse * glm::vec4(ndc, 1.0f, 1.0f);
    const glm::vec3 origin = glm::vec3(nearH) / nearH.w;
    const glm::vec3 dir = glm::normalize(glm::vec3(farH) / farH.w - origin);
    if (std::abs(dir.y) > 1e-4f) {
        const float t = -origin.y / dir.y;
        if (t > 0.0f && t < 500.0f)
            return origin + dir * t;
    }
    return origin + dir * 8.0f;
}

// Called right after the image, so the drop target is the image
void SceneViewPanel::acceptDrops(const OverlayContext& context, SceneActions& actions)
{
    if (!ImGui::BeginDragDropTarget())
        return;
    const ImVec2 mouse = ImGui::GetMousePos();
    if (const char* asset = acceptAssetDrop({AssetKind::Model, AssetKind::Prefab}))
        actions.instantiate(asset, entt::null, SceneActions::Placement::At, dropPoint(context, mouse.x, mouse.y));
    if (const char* material = acceptAssetDrop({AssetKind::Material})) {
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        m_pickX = static_cast<int>((mouse.x - m_view.imageX()) * scale.x);
        m_pickY = m_view.target().height() - 1 - static_cast<int>((mouse.y - m_view.imageY()) * scale.y);
        m_pickRequested = true;
        m_dropMaterial = material;
    }
    ImGui::EndDragDropTarget();
}

void SceneViewPanel::draw(const OverlayContext& context, EditHistory& history, Selection& selection,
                          SceneActions& actions, float gameAspect)
{
    if (m_dropTarget != entt::null) {
        actions.assignMaterial(m_dropTarget, m_dropMaterial);
        m_dropTarget = entt::null;
        m_dropMaterial.clear();
    }
    m_view.draw(nullptr, [&] {
        acceptDrops(context, actions);
        // No widget over the image: ImGuizmo only grabs the mouse when no item is hovered
        const bool clicked = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                             !ImGui::IsAnyItemHovered();
        const ImVec2 mouse = ImGui::GetMousePos();
        const ImGuiIO& io = ImGui::GetIO();
        const bool additive = io.KeyCtrl || io.KeyShift;

        Camera& camera = context.camera;
        camera.setAspect(m_view.aspect());
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(ImVec2(m_view.imageX(), m_view.imageY()),
                               ImVec2(m_view.imageX() + m_view.imageWidth(), m_view.imageY() + m_view.imageHeight()),
                               true);
        m_gizmos.draw(drawList, context.scene, selection,
                      {camera.getProjection() * camera.getView(), {m_view.imageX(), m_view.imageY()},
                       {m_view.imageWidth(), m_view.imageHeight()}, gameAspect});
        drawList->PopClipRect();

        drawToolbar(context);
        drawPrefabBar();
        drawGizmo(context, history, selection);
        handleHotkeys(context);

        if (clicked && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            if (const entt::entity icon = m_gizmos.pick({mouse.x, mouse.y}); icon != entt::null) {
                applyPick(selection, icon, additive);
            } else {
                const ImVec2 scale = io.DisplayFramebufferScale;
                m_pickX = static_cast<int>((mouse.x - m_view.imageX()) * scale.x);
                m_pickY = m_view.target().height() - 1 - static_cast<int>((mouse.y - m_view.imageY()) * scale.y);
                m_pickRequested = true;
                m_pickAdditive = additive;
            }
        }
    });
}

void SceneViewPanel::drawToolbar(const OverlayContext& context)
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

    // Edit mode only: playing always animates. Never saved, never an undo step.
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
    const bool editing = context.playMode == PlayMode::Edit;
    const bool previewing = editing && context.animationPreview;
    if (previewing)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.8f, 1.0f));
    ImGui::BeginDisabled(!editing);
    if (ImGui::SmallButton("Preview"))
        context.animationPreview = !context.animationPreview;
    ImGui::EndDisabled();
    if (previewing)
        ImGui::PopStyleColor();
    ImGui::SetItemTooltip(editing ? "Play Animators in Edit mode; off returns them to the bind pose"
                                  : "Animators play while the game runs");
}

void SceneViewPanel::setPrefab(const std::string& name, bool dirty)
{
    if (name != m_prefab)
        m_view.setTitle(name.empty() ? std::string("Scene") : "Prefab: " + name);
    m_prefab = name;
    m_prefabDirty = dirty;
}

// Top right of the image: "< Back  Prefab: Name*  Save"
void SceneViewPanel::drawPrefabBar()
{
    if (m_prefab.empty())
        return;
    const ImGuiStyle& style = ImGui::GetStyle();
    const std::string label = "Prefab: " + m_prefab + (m_prefabDirty ? "*" : "");
    const float width = ImGui::CalcTextSize("< Back").x + ImGui::CalcTextSize(label.c_str()).x +
                        ImGui::CalcTextSize("Save").x + style.FramePadding.x * 4.0f + style.ItemSpacing.x * 2.0f;
    ImGui::SetCursorScreenPos(ImVec2(m_view.imageX() + m_view.imageWidth() - width - 8.0f, m_view.imageY() + 8.0f));
    if (ImGui::SmallButton("< Back"))
        m_backRequested = true;
    ImGui::SetItemTooltip("Close the prefab and return to the scene");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.7f, 1.0f, 1.0f), "%s", label.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_prefabDirty);
    if (ImGui::SmallButton("Save"))
        m_saveRequested = true;
    ImGui::EndDisabled();
}

void SceneViewPanel::handleHotkeys(const OverlayContext& context)
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
}

// The local position, rotation and scale that put e at world
static bool localFromWorld(const entt::registry& registry, entt::entity e, const glm::mat4& world,
                           glm::vec3& translation, glm::quat& rotation, glm::vec3& scale)
{
    glm::mat4 parentWorld(1.0f);
    const entt::entity parent = registry.get<Relationship>(e).parent;
    if (parent != entt::null) {
        if (const WorldTransform* w = registry.try_get<WorldTransform>(parent))
            parentWorld = w->matrix;
    }
    glm::vec3 skew;
    glm::vec4 perspective;
    return glm::decompose(glm::inverse(parentWorld) * world, scale, rotation, translation, skew, perspective);
}

static void setWorld(entt::registry& registry, entt::entity e, const glm::mat4& world)
{
    glm::vec3 translation, scale;
    glm::quat rotation;
    if (!localFromWorld(registry, e, world, translation, rotation, scale))
        return;
    Transform& transform = registry.get<Transform>(e);
    transform.setPos(translation);
    transform.setRot(rotation);
    transform.setScale(scale);
}

void SceneViewPanel::beginDrag(const OverlayContext& context, const Selection& selection)
{
    entt::registry& registry = context.scene.registry();
    const EditContext edit{context.scene, context.components};
    m_drag.clear();
    for (const entt::entity e : selectionRoots(edit, selection)) {
        if (registry.all_of<Transform, WorldTransform>(e))
            m_drag.push_back({e, registry.get<Transform>(e), registry.get<WorldTransform>(e).matrix});
    }
    m_dragActive = selection.active();
    m_dragActiveStart = registry.get<WorldTransform>(m_dragActive).matrix;
    m_dragActiveScale = registry.get<Transform>(m_dragActive).getScale();
}

void SceneViewPanel::endDrag(const OverlayContext& context, EditHistory& history)
{
    entt::registry& registry = context.scene.registry();
    EditContext edit{context.scene, context.components};
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (const DragItem& item : m_drag) {
        if (!registry.valid(item.entity) || !registry.all_of<Transform>(item.entity))
            continue;
        if (auto command = transformCommand(edit, item.entity, item.start, registry.get<Transform>(item.entity)))
            commands.push_back(std::move(command));
    }
    if (commands.size() == 1)
        history.push(std::move(commands.front()));
    else if (!commands.empty())
        history.push(groupCommand("Transform " + std::to_string(commands.size()) + " Objects", std::move(commands)));
    m_drag.clear();
    m_dragActive = entt::null;
}

// ImGuizmo moves the active object's world matrix; the change is then applied to each
// selection root like Unity's Pivot mode: moved by the same offset, rotated about its own
// position, scaled by the same factor
void SceneViewPanel::drawGizmo(const OverlayContext& context, EditHistory& history, const Selection& selection)
{
    entt::registry& registry = context.scene.registry();
    const entt::entity active = selection.active();
    // A bone socket's transform follows its joint
    const bool editable = m_tool != Tool::None && active != entt::null && registry.valid(active) &&
                          registry.all_of<Transform, WorldTransform>(active) && !registry.all_of<BoneSocket>(active);

    // Drag ended (or its object went away): one complete edit
    if (m_dragActive != entt::null && (!ImGuizmo::IsUsing() || !editable || active != m_dragActive))
        endDrag(context, history);
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

    glm::mat4 world = registry.get<WorldTransform>(active).matrix;
    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation,
                                              m_local ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(world),
                                              nullptr, snapping ? snap : nullptr);

    if (ImGuizmo::IsUsing() && m_dragActive == entt::null)
        beginDrag(context, selection);
    if (!changed || m_dragActive == entt::null)
        return;

    if (m_tool == Tool::Scale) {
        // The active object's new local scale, relative to where the drag started
        glm::vec3 translation, now;
        glm::quat rotation;
        if (!localFromWorld(registry, active, world, translation, rotation, now))
            return;
        glm::vec3 ratio(1.0f);
        for (int i = 0; i < 3; ++i) {
            if (std::abs(m_dragActiveScale[i]) > 1e-6f)
                ratio[i] = now[i] / m_dragActiveScale[i];
        }
        for (const DragItem& item : m_drag) {
            if (!registry.valid(item.entity))
                continue;
            Transform& transform = registry.get<Transform>(item.entity);
            transform = item.start;
            transform.setScale(item.start.getScale() * ratio);
        }
        return;
    }

    const glm::mat4 delta = world * glm::inverse(m_dragActiveStart);
    for (const DragItem& item : m_drag) {
        if (!registry.valid(item.entity))
            continue;
        glm::mat4 moved;
        if (m_tool == Tool::Rotate) {
            const glm::vec3 pivot(item.startWorld[3]);
            const glm::mat4 rotation = glm::mat4(glm::mat3(delta));
            moved = glm::translate(glm::mat4(1.0f), pivot) * rotation * glm::translate(glm::mat4(1.0f), -pivot) *
                    item.startWorld;
        } else {
            moved = delta * item.startWorld;
        }
        setWorld(registry, item.entity, moved);
    }
}

void SceneViewPanel::render(const OverlayContext& context, Selection& selection)
{
    if (!m_view.visible()) {
        m_pickRequested = false;
        m_dropMaterial.clear();
        return;
    }
    GpuProfiler& profiler = context.renderer.profiler();
    GpuScope viewScope(profiler, "Scene View");
    const RenderTarget& target = m_view.target();
    Camera& camera = context.camera;
    camera.setAspect(m_view.aspect());
    const ViewMatrices view{camera.getView(), camera.getProjection()};
    const glm::mat4 viewProj = view.viewProj();
    {
        GpuScope scope(profiler, "Scene");
        context.renderer.render(view, target);
    }

    const entt::registry& registry = context.scene.registry();
    const bool hasSelection = !selection.empty();

    if (m_pickRequested) {
        GpuScope scope(profiler, "Pick IDs");
        m_pickRequested = false;
        m_pick.resize(target.width(), target.height());
        context.renderer.renderIds(viewProj, m_pick);
        const std::uint32_t id = m_pick.read(m_pickX, m_pickY);
        const auto e = static_cast<entt::entity>(id);
        const entt::entity hit = id != PickBuffer::NoId && registry.valid(e) ? e : entt::null;
        if (!m_dropMaterial.empty()) {
            if (hit != entt::null && registry.any_of<MeshRenderer, SkinnedMeshRenderer>(hit))
                m_dropTarget = hit;
            else
                m_dropMaterial.clear();
        } else {
            applyPick(selection, hit, m_pickAdditive);
        }
    }
    if (hasSelection && context.renderer.hasHighlight()) {
        {
            GpuScope scope(profiler, "Outline mask");
            context.renderer.renderMask(viewProj, target, m_outline);
        }
        GpuScope scope(profiler, "Outline");
        const int width = static_cast<int>(2.0f * ImGui::GetIO().DisplayFramebufferScale.x + 0.5f);
        context.renderer.drawOutline(target, m_outline, OutlineColor, width);
    }
}

void SceneViewPanel::release()
{
    m_view.release();
    m_pick.release();
    m_outline.release();
}
