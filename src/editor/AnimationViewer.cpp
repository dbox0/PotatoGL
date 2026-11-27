#include "AnimationViewer.h"

#include "editor/AssetOps.h"

#include "engine/anim/Clip.h"
#include "engine/anim/Pose.h"
#include "engine/app/Overlay.h"
#include "engine/assets/AssetCache.h"
#include "engine/reflect/ComponentRegistry.h"
#include "engine/render/Renderer.h"
#include "engine/scene/Components.h"
#include "engine/scene/Scene.h"
#include "engine/scene/Transform.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace
{
constexpr float Fov = glm::radians(45.0f);
constexpr float MinW = 1e-3f;
constexpr const char* DockName = "AnimationViewerDock";
constexpr const char* ClipsWindow = "Clips";
constexpr const char* TimelineWindow = "Timeline";
constexpr const char* SkeletonWindow = "Skeleton";
constexpr const char* JointWindow = "Joint";

const ImU32 GridColor = IM_COL32(255, 255, 255, 28);
const ImU32 GridAxisColor = IM_COL32(255, 255, 255, 70);
const ImU32 BoneColor = IM_COL32(120, 220, 255, 220);
const ImU32 JointColor = IM_COL32(120, 220, 255, 255);
const ImU32 SelectedColor = IM_COL32(255, 140, 30, 255);

std::string clipName(const std::string& ref)
{
    const std::size_t hash = ref.find('#');
    return hash == std::string::npos ? std::filesystem::path(ref).stem().string() : ref.substr(hash + 1);
}

struct Projector
{
    glm::mat4 viewProj;
    glm::vec2 origin;
    glm::vec2 size;

    ImVec2 toScreen(const glm::vec4& clip) const
    {
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        return ImVec2(origin.x + (ndc.x * 0.5f + 0.5f) * size.x, origin.y + (0.5f - ndc.y * 0.5f) * size.y);
    }

    void line(ImDrawList* dl, const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness = 1.0f) const
    {
        glm::vec4 ca = viewProj * glm::vec4(a, 1.0f);
        glm::vec4 cb = viewProj * glm::vec4(b, 1.0f);
        if (ca.w < MinW && cb.w < MinW)
            return;
        if (ca.w < MinW)
            ca = glm::mix(ca, cb, (MinW - ca.w) / (cb.w - ca.w));
        else if (cb.w < MinW)
            cb = glm::mix(cb, ca, (MinW - cb.w) / (ca.w - cb.w));
        dl->AddLine(toScreen(ca), toScreen(cb), color, thickness);
    }
};
}  // namespace

// One Poser step: the viewer's pose (and an authored clip) before and after
class PoserCommand final : public EditCommand
{
private:
    AnimationViewer& m_viewer;
    std::string m_name;
    AnimationViewer::PoserState m_before;
    AnimationViewer::PoserState m_after;

public:
    PoserCommand(AnimationViewer& viewer, std::string name, AnimationViewer::PoserState before,
                 AnimationViewer::PoserState after)
        : m_viewer(viewer), m_name(std::move(name)), m_before(std::move(before)), m_after(std::move(after))
    {
    }
    bool undo(EditContext&) override
    {
        m_viewer.restore(m_before);
        return true;
    }
    bool redo(EditContext&) override
    {
        m_viewer.restore(m_after);
        return true;
    }
    std::string name() const override { return m_name; }
};

AnimationViewer::AnimationViewer() = default;
AnimationViewer::~AnimationViewer() = default;

bool AnimationViewer::open(AssetCache& assets, Renderer& renderer, const std::string& model,
                           const std::string& material, const std::string& clip, std::string* error)
{
    const ModelId id = assets.model(model);
    std::shared_ptr<const SkeletonData> skeleton = id == InvalidModel ? nullptr : assets.skeleton(model);
    if (!skeleton) {
        if (error)
            *error = std::filesystem::path(model).filename().string() + " has no skeleton";
        return false;
    }
    close();
    m_scene = std::make_unique<Scene>();
    m_model = model;
    m_name = std::filesystem::path(model).filename().string();
    m_skeleton = std::move(skeleton);
    entt::registry& registry = m_scene->registry();

    // Normalized to about two metres, standing on the ground at the origin
    const Renderer::Bounds bounds = renderer.modelBounds(id);
    const glm::vec3 size = bounds.max - bounds.min;
    const float extent = std::max({size.x, size.y, size.z});
    const float scale = extent > 1e-6f ? 2.0f / extent : 1.0f;
    const glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
    m_actorHeight = std::max(size.y * scale, 0.1f);
    m_actor = m_scene->createGameObject(m_name);
    Transform& actor = registry.get<Transform>(m_actor);
    actor.setScale(glm::vec3(scale));
    actor.setPos(glm::vec3(-center.x, -bounds.min.y, -center.z) * scale);
    SkinnedMeshRenderer& skinned = registry.emplace<SkinnedMeshRenderer>(m_actor);
    skinned.mesh = model;
    skinned.material = material;
    registry.emplace<Animator>(m_actor).playOnStart = false;

    const entt::entity ground = m_scene->createGameObject("Ground");
    registry.emplace<MeshRenderer>(ground).mesh = "builtin:plane";

    const entt::entity light = m_scene->createGameObject("Light");
    registry.get<Transform>(light).setEulerAngles(glm::radians(glm::vec3(-50.0f, -30.0f, 0.0f)));
    registry.emplace<DirectionalLight>(light).intensity = 2.5f;

    m_children.assign(m_skeleton->jointCount(), {});
    for (std::size_t j = 0; j < m_skeleton->jointCount(); ++j) {
        if (m_skeleton->parents[j] >= 0)
            m_children[static_cast<std::size_t>(m_skeleton->parents[j])].push_back(static_cast<int>(j));
    }

    m_skeletonHash = skeletonHash(*m_skeleton);
    m_rig = detectHumanoid(*m_skeleton);
    m_poles.assign(m_rig.limbs.size(), std::nullopt);
    m_pins.assign(m_rig.limbs.size(), Pin{});
    m_poleLimb = -1;
    m_planted.clear();
    m_clips = assets.clipRefs(model);
    m_embeddedClips = m_clips.size();
    refreshAssets();
    const auto start = std::find(m_clips.begin(), m_clips.end(), clip);
    m_history.clear();
    m_overrides.clear();
    m_posing = false;
    m_baseBind = false;
    m_status.clear();
    m_joint = -1;
    m_loop = true;
    m_speed = 1.0f;
    selectClip(assets, start == m_clips.end() ? 0 : static_cast<int>(start - m_clips.begin()));
    frameActor();
    m_view.focus();
    return true;
}

void AnimationViewer::close()
{
    m_scene.reset();
    m_actor = entt::null;
    m_skeleton.reset();
    m_clipData.reset();
    m_clips.clear();
    m_poses.clear();
    m_embeddedClips = 0;
    m_clip = -1;
    m_authored.reset();
    m_overrides.clear();
    m_history.clear();
    m_posing = false;
    m_gizmoDragging = false;
    m_fieldStart.reset();
    m_children.clear();
    m_markers.clear();
    m_timelineDrag = Drag::None;
    m_orbitButton = -1;
    m_panning = false;
    m_pressed = false;
}

void AnimationViewer::release()
{
    m_view.release();
}

glm::mat4 AnimationViewer::viewMatrix() const
{
    const glm::vec3 dir(std::cos(m_pitch) * std::sin(m_yaw), std::sin(m_pitch), std::cos(m_pitch) * std::cos(m_yaw));
    return glm::lookAt(m_target + dir * m_distance, m_target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 AnimationViewer::projection() const
{
    return glm::perspective(Fov, m_view.aspect(), std::max(m_distance * 0.01f, 0.01f), m_distance * 10.0f + 20.0f);
}

void AnimationViewer::frameActor()
{
    m_target = glm::vec3(0.0f, m_actorHeight * 0.5f, 0.0f);
    const float radius = std::max(m_actorHeight, 2.0f) * 0.6f;
    m_distance = radius / std::sin(Fov * 0.5f);
}

void AnimationViewer::selectClip(AssetCache& assets, int index)
{
    if (index < 0 || index >= static_cast<int>(m_clips.size())) {
        m_clip = -1;
        m_clipData.reset();
        m_in = m_out = 0;
        m_time = 0.0f;
        return;
    }
    m_clip = index;
    const std::string& ref = m_clips[static_cast<std::size_t>(index)];
    m_clipData = assets.clip(ref);
    m_authored.reset();
    if (ref.find('#') == std::string::npos) {
        AuthoredClip authored;
        std::string error;
        const auto text = assetops::readText(assetops::fromRef(ref));
        if (text && loadAnimText(*text, &authored, nullptr, {}, &error))
            m_authored = std::move(authored);
        else
            m_status = "Can't read the keys of " + clipName(ref) + (error.empty() ? "" : ": " + error);
    }
    m_in = 0;
    m_out = lastFrame();
    m_time = 0.0f;
    m_playing = true;
}

int AnimationViewer::lastFrame() const
{
    return m_clipData && m_clipData->frameCount > 0 ? static_cast<int>(m_clipData->frameCount) - 1 : 0;
}

float AnimationViewer::frameTime(int frame) const
{
    if (!m_clipData)
        return 0.0f;
    return std::min(static_cast<float>(frame) / m_clipData->rate, m_clipData->duration);
}

int AnimationViewer::currentFrame() const
{
    if (!m_clipData)
        return 0;
    return std::clamp(static_cast<int>(std::lround(m_time * m_clipData->rate)), 0, lastFrame());
}

void AnimationViewer::stepFrame(int delta)
{
    m_playing = false;
    m_time = frameTime(std::clamp(currentFrame() + delta, 0, lastFrame()));
}

// Plays within [in, out], wrapping or stopping at the ends
void AnimationViewer::advance(float dt)
{
    if (!m_playing || !m_clipData)
        return;
    const float start = frameTime(m_in), end = frameTime(m_out);
    const float length = end - start;
    if (length <= 0.0f) {
        m_time = start;
        return;
    }
    float t = m_time + dt * m_speed;
    if (m_loop) {
        t = std::fmod(t - start, length);
        m_time = start + (t < 0.0f ? t + length : t);
        return;
    }
    if (t >= end || t <= start) {
        m_playing = false;
        t = std::clamp(t, start, end);
    }
    m_time = t;
}

// The Animator samples the viewer's time; the AnimationSystem only evaluates it here
void AnimationViewer::applyTime()
{
    Animator& animator = m_scene->registry().get<Animator>(m_actor);
    const std::string ref = m_posing && m_baseBind ? std::string() : clipRef();
    animator.overrides.clear();
    if (m_posing)
        animator.overrides.assign(m_overrides.begin(), m_overrides.end());
    if (animator.clip != ref) {
        animator.clip = ref;
        animator.resolved = false;
    }
    animator.time = m_time;
    animator.evaluated = false;
}

void AnimationViewer::draw(const OverlayContext& context)
{
    if (!isOpen())
        return;
    m_components = &context.components;
    m_assets = &context.assets;
    m_history.endFrame({}, {}, {});
    drawDockSpace();
    advance(ImGui::GetIO().DeltaTime);
    drawViewport();
    drawClips(context.assets);
    drawTimeline();
    drawSkeleton();
    drawJoint();
    drawPopups();
    handleShortcuts();
    if (isOpen() && m_posing && !m_gizmoDragging)
        applyPins();
    if (isOpen())
        applyTime();
}

void AnimationViewer::render(const OverlayContext& context)
{
    if (!isOpen() || !m_view.visible())
        return;
    GpuScope scope(context.renderer.profiler(), "Animation Viewer");
    context.renderer.render(ViewMatrices{viewMatrix(), projection()}, m_view.target());
}

void AnimationViewer::drawDockSpace()
{
    const ImGuiID dockId = ImGui::GetID(DockName);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!ImGui::DockBuilderGetNode(dockId)) {
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, viewport->WorkSize);
        ImGuiID center = dockId;
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
        const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.4f, nullptr, &right);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.2f, nullptr, &center);
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
        ImGui::DockBuilderDockWindow(m_view.name(), center);
        ImGui::DockBuilderDockWindow(ClipsWindow, left);
        ImGui::DockBuilderDockWindow(TimelineWindow, bottom);
        ImGui::DockBuilderDockWindow(SkeletonWindow, right);
        ImGui::DockBuilderDockWindow(JointWindow, rightBottom);
        ImGui::DockBuilderFinish(dockId);
    }
    ImGui::DockSpaceOverViewport(dockId, viewport);
}

void AnimationViewer::drawViewport()
{
    m_view.setTitle("Animation Viewer: " + m_name);
    m_view.draw(nullptr, [&] {
        handleCamera();
        drawViewportOverlay();
        drawJointGizmo();

        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::SetCursorScreenPos(ImVec2(m_view.imageX() + 8.0f, m_view.imageY() + 8.0f));
        ImGui::TextColored(ImVec4(0.45f, 0.7f, 1.0f, 1.0f), "%s", m_name.c_str());
        if (m_clip >= 0) {
            ImGui::SameLine();
            ImGui::TextUnformatted(clipName(m_clips[static_cast<std::size_t>(m_clip)]).c_str());
        }
        if (m_posing) {
            ImGui::SetCursorScreenPos(ImVec2(m_view.imageX() + 8.0f, m_view.imageY() + 8.0f + ImGui::GetFrameHeightWithSpacing()));
            struct Button
            {
                const char* label;
                GizmoOp op;
            };
            static constexpr Button buttons[] = {{"W Move", GizmoOp::Move}, {"E Rotate", GizmoOp::Rotate}, {"R Scale", GizmoOp::Scale}};
            for (const Button& b : buttons) {
                const bool active = m_gizmoOp == b.op;
                if (active)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::SmallButton(b.label))
                    m_gizmoOp = b.op;
                if (active)
                    ImGui::PopStyleColor();
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(m_gizmoLocal ? "X Local" : "X Global"))
                m_gizmoLocal = !m_gizmoLocal;
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Pose mode");
        }
        const char* closeLabel = "Close Viewer";
        const float width = ImGui::CalcTextSize(closeLabel).x + style.FramePadding.x * 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(m_view.imageX() + m_view.imageWidth() - width - 8.0f, m_view.imageY() + 8.0f));
        if (ImGui::SmallButton(closeLabel))
            m_closeRequested = true;
        ImGui::SetItemTooltip("Back to the scene (Escape)");
    });
}

// Called right after the image, so IsItemHovered() is the image
void AnimationViewer::handleCamera()
{
    const ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && io.MouseWheel != 0.0f)
        m_distance = std::clamp(m_distance * std::pow(0.88f, io.MouseWheel), 0.2f, 200.0f);

    if (hovered && m_orbitButton < 0) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            m_orbitButton = ImGuiMouseButton_Right;
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyAlt)
            m_orbitButton = ImGuiMouseButton_Left;
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_gizmoHovered) {
            m_pressed = true;
            m_pressPos = glm::vec2(io.MousePos.x, io.MousePos.y);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            m_panning = true;
    }
    if (m_orbitButton >= 0) {
        if (ImGui::IsMouseDown(m_orbitButton)) {
            m_yaw -= io.MouseDelta.x * 0.008f;
            m_pitch = std::clamp(m_pitch + io.MouseDelta.y * 0.008f, -1.5f, 1.5f);
        } else {
            m_orbitButton = -1;
        }
    }
    if (m_panning) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            const glm::mat4 view = viewMatrix();
            const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
            const glm::vec3 up(view[0][1], view[1][1], view[2][1]);
            const float k = m_distance * 0.0016f;
            m_target += (-right * io.MouseDelta.x + up * io.MouseDelta.y) * k;
        } else {
            m_panning = false;
        }
    }

    // A click without a drag picks the nearest joint marker
    if (m_pressed && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_pressed = false;
        const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
        bool handled = false;
        if (glm::length(mouse - m_pressPos) < 4.0f) {
            const float handleRadius = ImGui::GetFontSize() * 0.75f;
            const Handle* hit = nullptr;
            for (const Handle& handle : m_handles) {
                if (glm::length(mouse - handle.pos) <= handleRadius && (!hit || handle.depth < hit->depth))
                    hit = &handle;
            }
            if (hit) {
                handled = true;
                // Grabbing a handle is what Pose mode is for
                if (!m_posing) {
                    m_posing = true;
                    m_playing = false;
                }
                if (hit->kind == HandleKind::Pole) {
                    m_poleLimb = hit->index;
                } else {
                    m_poleLimb = -1;
                    m_joint = hit->kind == HandleKind::Hips ? hit->index
                                                            : effectorJoint(m_rig.limbs[static_cast<std::size_t>(hit->index)]);
                    m_gizmoOp = GizmoOp::Move;
                    m_revealJoint = true;
                }
            }
        }
        if (!handled && glm::length(mouse - m_pressPos) < 4.0f) {
            const float radius = ImGui::GetFontSize() * 0.6f;
            int best = -1;
            float bestDepth = 0.0f;
            for (const Marker& marker : m_markers) {
                if (glm::length(mouse - marker.pos) <= radius && (best < 0 || marker.depth < bestDepth)) {
                    best = marker.joint;
                    bestDepth = marker.depth;
                }
            }
            if (best >= 0) {
                m_joint = best;
                m_poleLimb = -1;
                m_revealJoint = true;
            }
        }
    }
}

// Ground grid, bones and a clickable marker per joint
void AnimationViewer::drawViewportOverlay()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const glm::vec2 origin(m_view.imageX(), m_view.imageY());
    const glm::vec2 size(m_view.imageWidth(), m_view.imageHeight());
    dl->PushClipRect(ImVec2(origin.x, origin.y), ImVec2(origin.x + size.x, origin.y + size.y), true);
    const Projector p{projection() * viewMatrix(), origin, size};

    constexpr int Lines = 10;
    constexpr float Spacing = 0.5f;
    const float half = Lines * Spacing;
    for (int i = -Lines; i <= Lines; ++i) {
        const float x = static_cast<float>(i) * Spacing;
        const ImU32 color = i == 0 ? GridAxisColor : GridColor;
        p.line(dl, {x, 0.001f, -half}, {x, 0.001f, half}, color);
        p.line(dl, {-half, 0.001f, x}, {half, 0.001f, x}, color);
    }

    m_markers.clear();
    const entt::registry& registry = m_scene->registry();
    const SkinnedMeshRenderer& skinned = registry.get<SkinnedMeshRenderer>(m_actor);
    if (skinned.skeleton && skinned.jointModel.size() == skinned.skeleton->jointCount()) {
        const glm::mat4& world = registry.get<WorldTransform>(m_actor).matrix;
        const std::vector<std::int16_t>& parents = skinned.skeleton->parents;
        std::vector<glm::vec3> points(parents.size());
        for (std::size_t j = 0; j < parents.size(); ++j)
            points[j] = glm::vec3(world * glm::vec4(skinned.jointModel[j].translation(), 1.0f));
        for (std::size_t j = 0; j < parents.size(); ++j) {
            const int parent = parents[j];
            if (parent >= 0 && parents[static_cast<std::size_t>(parent)] >= 0)
                p.line(dl, points[static_cast<std::size_t>(parent)], points[j], BoneColor, 1.5f);
        }
        const float radius = ImGui::GetFontSize() * 0.22f;
        for (std::size_t j = 0; j < parents.size(); ++j) {
            const glm::vec4 clip = p.viewProj * glm::vec4(points[j], 1.0f);
            if (clip.w < MinW)
                continue;
            const ImVec2 screen = p.toScreen(clip);
            const bool selected = static_cast<int>(j) == m_joint;
            dl->AddCircleFilled(screen, selected ? radius * 1.6f : radius, selected ? SelectedColor : JointColor);
            m_markers.push_back({static_cast<int>(j), glm::vec2(screen.x, screen.y), clip.w});
        }
    }
    m_handles.clear();
    if (m_ik && !m_rig.limbs.empty())
        drawIkHandles(dl, p.viewProj, origin, size);
    dl->PopClipRect();
}

void AnimationViewer::drawClips(AssetCache& assets)
{
    if (!ImGui::Begin(ClipsWindow)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("%s", m_model.c_str());
    auto clipRow = [&](std::size_t i) {
        ImGui::PushID(static_cast<int>(i));
        const std::shared_ptr<const ClipData> clip = assets.clip(m_clips[i]);
        if (ImGui::Selectable(clipName(m_clips[i]).c_str(), static_cast<int>(i) == m_clip))
            selectClip(assets, static_cast<int>(i));
        if (i >= m_embeddedClips)
            ImGui::SetItemTooltip("%s", assetops::displayPath(m_clips[i]).c_str());
        if (clip) {
            char length[32];
            std::snprintf(length, sizeof(length), "%.2f s", clip->duration);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(length).x);
            ImGui::TextDisabled("%s", length);
        }
        ImGui::PopID();
    };

    ImGui::SeparatorText("Embedded");
    if (m_embeddedClips == 0)
        ImGui::TextDisabled("This model has no clips");
    for (std::size_t i = 0; i < m_embeddedClips; ++i)
        clipRow(i);

    ImGui::SeparatorText("Authored");
    if (ImGui::SmallButton("New")) {
        m_nameAction = NameAction::NewClip;
        std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "New Clip");
        m_openNamePopup = true;
    }
    ImGui::SetItemTooltip("An empty .anim next to the model");
    ImGui::SameLine();
    ImGui::BeginDisabled(m_clip < 0);
    if (ImGui::SmallButton("Duplicate")) {
        m_nameAction = NameAction::DuplicateClip;
        std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "%s", clipName(clipRef()).c_str());
        m_openNamePopup = true;
    }
    ImGui::SetItemTooltip("An editable .anim copy of the selected clip");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_authored);
    if (ImGui::SmallButton("Delete")) {
        m_deleteRef = clipRef();
        m_openDeletePopup = true;
    }
    ImGui::EndDisabled();
    if (m_clips.size() == m_embeddedClips)
        ImGui::TextDisabled("No .anim clips for this skeleton yet");
    for (std::size_t i = m_embeddedClips; i < m_clips.size(); ++i)
        clipRow(i);

    ImGui::SeparatorText("Poses");
    ImGui::BeginDisabled(m_overrides.empty());
    if (ImGui::SmallButton("Save Pose")) {
        m_nameAction = NameAction::SavePose;
        std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "New Pose");
        m_openNamePopup = true;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("The posed joints as a .pose next to the model");
    for (std::size_t i = 0; i < m_poses.size(); ++i) {
        ImGui::PushID(static_cast<int>(i + 10000));
        if (ImGui::Selectable(clipName(m_poses[i]).c_str()))
            openAsset(assets, m_poses[i]);
        ImGui::SetItemTooltip("Apply %s", assetops::displayPath(m_poses[i]).c_str());
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete")) {
                m_deleteRef = m_poses[i];
                m_openDeletePopup = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::End();
}

void AnimationViewer::drawTimeline()
{
    if (!ImGui::Begin(TimelineWindow)) {
        ImGui::End();
        return;
    }
    const int last = lastFrame();
    ImGui::BeginDisabled(!m_clipData);
    if (ImGui::Button("|<"))
        stepFrame(m_in - currentFrame());
    ImGui::SetItemTooltip("To the in frame (Home)");
    ImGui::SameLine();
    if (ImGui::Button("<"))
        stepFrame(-1);
    ImGui::SetItemTooltip("Previous frame (Left)");
    ImGui::SameLine();
    if (ImGui::Button(m_playing ? "Pause" : "Play", ImVec2(ImGui::CalcTextSize("Pause").x + ImGui::GetStyle().FramePadding.x * 2.0f, 0.0f))) {
        if (!m_playing && !m_loop && currentFrame() >= m_out)
            m_time = frameTime(m_in);
        m_playing = !m_playing;
    }
    ImGui::SetItemTooltip("Space");
    ImGui::SameLine();
    if (ImGui::Button(">"))
        stepFrame(1);
    ImGui::SetItemTooltip("Next frame (Right)");
    ImGui::SameLine();
    if (ImGui::Button(">|"))
        stepFrame(m_out - currentFrame());
    ImGui::SetItemTooltip("To the out frame (End)");
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &m_loop);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("Speed", &m_speed, 0.01f, -4.0f, 4.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::DragInt("In", &m_in, 0.2f, 0, m_out))
        m_in = std::clamp(m_in, 0, m_out);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::DragInt("Out", &m_out, 0.2f, m_in, last))
        m_out = std::clamp(m_out, m_in, last);
    if (m_authored) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        int length = m_authored->frameCount;
        if (ImGui::InputInt("Length", &length, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue) &&
            std::max(length, 1) != m_authored->frameCount) {
            PoserState before = state();
            m_authored->frameCount = std::max(length, 1);
            commitAuthored();
            record("Clip Length", std::move(before));
        }
        ImGui::SetItemTooltip("Frames in the clip");
    }
    ImGui::SameLine();
    const float rate = m_clipData ? m_clipData->rate : ClipData::DefaultRate;
    ImGui::Text("Frame %d / %d   %.2f / %.2f s   %.0f fps", currentFrame(), last, m_time,
                m_clipData ? m_clipData->duration : 0.0f, rate);
    ImGui::EndDisabled();

    // Scrub bar: frame ticks, in/out range, key ticks of the selected joint, playhead
    const float height = std::max(ImGui::GetContentRegionAvail().y, 40.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 50.0f);
    ImGui::InvisibleButton("##scrub", ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float pad = 10.0f;
    const float x0 = pos.x + pad, x1 = pos.x + width - pad;
    auto frameX = [&](float frame) { return last > 0 ? x0 + (x1 - x0) * frame / static_cast<float>(last) : x0; };
    auto xFrame = [&](float x) {
        return last > 0 ? std::clamp(static_cast<int>(std::lround((x - x0) / (x1 - x0) * static_cast<float>(last))), 0, last) : 0;
    };
    const float top = pos.y, bottom = pos.y + height;
    dl->AddRectFilled(pos, ImVec2(pos.x + width, bottom), IM_COL32(28, 28, 32, 255), 3.0f);
    dl->AddRectFilled(ImVec2(frameX(static_cast<float>(m_in)), top), ImVec2(frameX(static_cast<float>(m_out)), bottom),
                      IM_COL32(60, 64, 76, 255));

    const float spacing = last > 0 ? (x1 - x0) / static_cast<float>(last) : x1 - x0;
    int step = 1;
    for (const int candidate : {1, 2, 5, 10, 20, 50, 100, 200, 500}) {
        step = candidate;
        if (spacing * static_cast<float>(candidate) >= 6.0f)
            break;
    }
    for (int f = 0; f <= last; f += step) {
        const bool major = f % (step * 5) == 0;
        const float x = frameX(static_cast<float>(f));
        dl->AddLine(ImVec2(x, top + 14.0f), ImVec2(x, top + (major ? 26.0f : 20.0f)),
                    major ? IM_COL32(200, 200, 200, 160) : IM_COL32(200, 200, 200, 70));
        if (major) {
            char label[16];
            std::snprintf(label, sizeof(label), "%d", f);
            dl->AddText(ImVec2(x + 2.0f, top + 1.0f), IM_COL32(200, 200, 200, 150), label);
        }
    }

    // Authored clips: the selected joint's keys, or every key dimmed if no joint is selected
    auto keyMark = [&](int frame, ImU32 color) {
        const float x = frameX(static_cast<float>(frame));
        const float y = bottom - 7.0f;
        dl->AddQuadFilled(ImVec2(x, y - 4.0f), ImVec2(x + 4.0f, y), ImVec2(x, y + 4.0f), ImVec2(x - 4.0f, y), color);
    };
    if (m_authored && m_skeleton) {
        for (const AuthoredTrack& track : m_authored->tracks) {
            const bool selected = m_joint >= 0 && track.joint == m_skeleton->names[static_cast<std::size_t>(m_joint)];
            if (m_joint >= 0 && !selected)
                continue;
            const ImU32 color = selected ? IM_COL32(255, 210, 90, 255) : IM_COL32(200, 200, 200, 90);
            for (const auto& k : track.t)
                keyMark(k.frame, color);
            for (const auto& k : track.r)
                keyMark(k.frame, color);
            for (const auto& k : track.s)
                keyMark(k.frame, color);
        }
    }
    // Embedded clips have a key on every frame of an animated channel
    else if (m_clipData && m_skeleton && m_joint >= 0) {
        const std::uint32_t hash = m_skeleton->nameHashes[static_cast<std::size_t>(m_joint)];
        for (const ClipData::Track& track : m_clipData->tracks) {
            if (track.jointHash != hash || track.animated == 0)
                continue;
            const float y = bottom - 7.0f;
            const int keyStep = spacing >= 3.0f ? 1 : step;
            for (int f = 0; f <= last; f += keyStep) {
                const float x = frameX(static_cast<float>(f));
                dl->AddQuadFilled(ImVec2(x, y - 3.0f), ImVec2(x + 3.0f, y), ImVec2(x, y + 3.0f), ImVec2(x - 3.0f, y),
                                  IM_COL32(110, 170, 255, 200));
            }
        }
    }

    auto handle = [&](int frame, ImU32 color) {
        const float x = frameX(static_cast<float>(frame));
        dl->AddTriangleFilled(ImVec2(x - 5.0f, top), ImVec2(x + 5.0f, top), ImVec2(x, top + 8.0f), color);
        dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), color & IM_COL32(255, 255, 255, 90));
    };
    handle(m_in, IM_COL32(120, 220, 120, 255));
    handle(m_out, IM_COL32(230, 110, 110, 255));
    const float playX = m_clipData && last > 0 ? frameX(m_time * m_clipData->rate) : x0;
    dl->AddLine(ImVec2(playX, top), ImVec2(playX, bottom), SelectedColor, 2.0f);

    if (m_clipData && ImGui::IsItemActivated()) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool nearTop = mouse.y < top + 12.0f;
        if (nearTop && std::abs(mouse.x - frameX(static_cast<float>(m_in))) < 6.0f)
            m_timelineDrag = Drag::In;
        else if (nearTop && std::abs(mouse.x - frameX(static_cast<float>(m_out))) < 6.0f)
            m_timelineDrag = Drag::Out;
        else
            m_timelineDrag = Drag::Playhead;
    }
    if (ImGui::IsItemActive() && m_timelineDrag != Drag::None) {
        const int frame = xFrame(ImGui::GetMousePos().x);
        if (m_timelineDrag == Drag::Playhead) {
            m_playing = false;
            m_time = frameTime(frame);
        } else if (m_timelineDrag == Drag::In) {
            m_in = std::min(frame, m_out);
        } else {
            m_out = std::max(frame, m_in);
        }
    } else {
        m_timelineDrag = Drag::None;
    }
    ImGui::End();
}

void AnimationViewer::drawSkeleton()
{
    if (ImGui::Begin(SkeletonWindow) && m_skeleton) {
        ImGui::TextDisabled("%zu joints", m_skeleton->jointCount());
        for (std::size_t j = 0; j < m_skeleton->jointCount(); ++j) {
            if (m_skeleton->parents[j] < 0)
                drawJointTree(static_cast<int>(j));
        }
        m_revealJoint = false;
    }
    ImGui::End();
}

void AnimationViewer::drawJointTree(int joint)
{
    const std::vector<int>& children = m_children[static_cast<std::size_t>(joint)];
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (joint == m_joint)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (m_revealJoint && m_joint >= 0) {
        for (int p = m_skeleton->parents[static_cast<std::size_t>(m_joint)]; p >= 0;
             p = m_skeleton->parents[static_cast<std::size_t>(p)]) {
            if (p == joint)
                ImGui::SetNextItemOpen(true);
        }
    }
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<std::intptr_t>(joint)), flags, "%s",
                                        m_skeleton->names[static_cast<std::size_t>(joint)].c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        m_joint = joint;
        m_poleLimb = -1;
    }
    if (m_revealJoint && joint == m_joint)
        ImGui::SetScrollHereY();
    if (open && !children.empty()) {
        for (const int child : children)
            drawJointTree(child);
        ImGui::TreePop();
    }
}

void AnimationViewer::drawJoint()
{
    if (!ImGui::Begin(JointWindow)) {
        ImGui::End();
        return;
    }
    drawPoser();
    if (m_joint < 0 || !m_skeleton) {
        ImGui::TextDisabled("Select a joint in the viewport or the Skeleton panel");
        ImGui::End();
        return;
    }
    const auto j = static_cast<std::size_t>(m_joint);
    const entt::registry& registry = m_scene->registry();
    const SkinnedMeshRenderer& skinned = registry.get<SkinnedMeshRenderer>(m_actor);

    ImGui::SeparatorText(m_skeleton->names[j].c_str());
    const int parent = m_skeleton->parents[j];
    ImGui::TextDisabled("Parent: %s", parent >= 0 ? m_skeleton->names[static_cast<std::size_t>(parent)].c_str() : "none");

    std::uint8_t animated = 0;
    if (m_clipData) {
        for (const ClipData::Track& track : m_clipData->tracks) {
            if (track.jointHash == m_skeleton->nameHashes[j])
                animated = track.animated;
        }
    }
    ImGui::TextDisabled("Animated: %s%s%s%s", animated & ClipData::Translation ? "position " : "",
                        animated & ClipData::Rotation ? "rotation " : "", animated & ClipData::Scale ? "scale" : "",
                        animated == 0 ? "no" : "");
    if (m_overrides.contains(m_joint))
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Posed");

    // Local transform; editable in Pose mode, each drag one undo step
    JointTrs local = displayedLocal(m_joint);
    glm::vec3 euler = glm::degrees(glm::eulerAngles(local.r));
    const float speed = 0.005f * std::max(1.0f, glm::length(m_skeleton->bindLocal[j].t));
    ImGui::PushItemWidth(-80.0f);
    ImGui::BeginDisabled(!m_posing);
    bool changed = false;
    auto track = [&](bool edited) {
        if (ImGui::IsItemActivated() && !m_fieldStart)
            m_fieldStart = state();
        changed |= edited;
        if (ImGui::IsItemDeactivated() && m_fieldStart) {
            if (ImGui::IsItemDeactivatedAfterEdit())
                record("Pose " + m_skeleton->names[j], std::move(*m_fieldStart));
            m_fieldStart.reset();
        }
    };
    track(ImGui::DragFloat3("Position", &local.t.x, speed));
    const bool rotated = ImGui::DragFloat3("Rotation", &euler.x, 0.5f);
    track(rotated);
    track(ImGui::DragFloat3("Scale", &local.s.x, 0.005f));
    ImGui::EndDisabled();
    if (changed) {
        if (rotated)
            local.r = glm::quat(glm::radians(euler));
        m_overrides[m_joint] = local;
    }
    ImGui::SeparatorText("Model space");
    glm::vec3 model = j < skinned.jointModel.size() ? skinned.jointModel[j].translation() : glm::vec3(0.0f);
    ImGui::BeginDisabled();
    ImGui::DragFloat3("Position##model", &model.x);
    ImGui::EndDisabled();
    ImGui::PopItemWidth();
    ImGui::End();
}

void AnimationViewer::drawPoser()
{
    // Pushed and popped on the state before the click, which toggles it
    const bool posing = m_posing;
    if (posing)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.42f, 0.12f, 1.0f));
    if (ImGui::Button(posing ? "Pose Mode: On" : "Pose Mode: Off")) {
        m_posing = !m_posing;
        if (m_posing)
            m_playing = false;
    }
    if (posing)
        ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Pose joints over the clip frame (or the bind pose) with the gizmo or the fields below");
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_history.canUndo());
    if (ImGui::Button("Undo")) {
        EditContext edit{*m_scene, *m_components};
        m_history.undo(edit);
    }
    ImGui::EndDisabled();
    if (m_history.canUndo())
        ImGui::SetItemTooltip("Undo %s (Ctrl+Z)", m_history.undoName().c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_history.canRedo());
    if (ImGui::Button("Redo")) {
        EditContext edit{*m_scene, *m_components};
        m_history.redo(edit);
    }
    ImGui::EndDisabled();
    if (m_history.canRedo())
        ImGui::SetItemTooltip("Redo %s (Ctrl+Y)", m_history.redoName().c_str());

    if (!m_posing) {
        if (!m_rig.limbs.empty())
            ImGui::TextWrapped("Click a hand, foot or hips handle in the viewport to start posing");
        drawIkPanel();
        if (!m_status.empty())
            ImGui::TextWrapped("%s", m_status.c_str());
        return;
    }
    int base = m_baseBind ? 1 : 0;
    ImGui::TextUnformatted("Base");
    ImGui::SameLine();
    ImGui::RadioButton("Clip frame", &base, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Bind pose", &base, 1);
    m_baseBind = base == 1;

    const bool joint = m_joint >= 0;
    ImGui::BeginDisabled(!joint);
    if (ImGui::SmallButton("Reset Joint")) {
        PoserState before = state();
        m_overrides.erase(m_joint);
        record("Reset Joint", std::move(before));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_overrides.empty());
    if (ImGui::SmallButton("Reset All")) {
        PoserState before = state();
        m_overrides.clear();
        record("Reset Pose", std::move(before));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Flip Pose"))
        flipPose();
    ImGui::SetItemTooltip("Mirror the posed joints left to right");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!joint);
    if (ImGui::SmallButton("Mirror Joint"))
        mirrorJoint();
    ImGui::SetItemTooltip("Give the joint on the other side this joint's pose, mirrored");
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!joint);
    if (ImGui::SmallButton("Copy Joint"))
        m_jointClipboard = displayedLocal(m_joint);
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_jointClipboard);
    if (ImGui::SmallButton("Paste Joint")) {
        PoserState before = state();
        m_overrides[m_joint] = *m_jointClipboard;
        record("Paste Joint", std::move(before));
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_overrides.empty());
    if (ImGui::SmallButton("Copy Pose")) {
        m_poseClipboard.clear();
        for (const auto& [j, local] : m_overrides)
            m_poseClipboard.push_back({m_skeleton->names[static_cast<std::size_t>(j)], local});
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_poseClipboard.empty());
    if (ImGui::SmallButton("Paste Pose")) {
        PoserState before = state();
        for (const PoseJoint& p : m_poseClipboard) {
            if (const int j = m_skeleton->findJoint(p.joint); j >= 0)
                m_overrides[j] = p.local;
        }
        record("Paste Pose", std::move(before));
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!m_authored || (m_overrides.empty() && !joint));
    if (ImGui::Button("Key Pose"))
        keyPose();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(m_authored ? "Key the posed joints (or the selected one) at the playhead (K)"
                                     : "Select or create an .anim clip to key into");
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_authored || !joint);
    if (ImGui::Button("Delete Key"))
        deleteKey();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Remove the selected joint's keys at the playhead");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu posed", m_overrides.size());
    drawIkPanel();
    if (!m_status.empty())
        ImGui::TextWrapped("%s", m_status.c_str());
}

void AnimationViewer::drawPopups()
{
    if (std::exchange(m_openNamePopup, false))
        ImGui::OpenPopup("Name##poser");
    if (std::exchange(m_openDeletePopup, false))
        ImGui::OpenPopup("Delete##poser");

    if (ImGui::BeginPopupModal("Name##poser", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* what = m_nameAction == NameAction::SavePose ? "Pose name" : "Clip name";
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        const bool entered = ImGui::InputText(what, m_nameBuffer, sizeof(m_nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string name = m_nameBuffer;
        const bool valid = assetops::validName(name);
        ImGui::TextDisabled("Saved in %s", assetops::displayPath(assetops::toRef(assetFolder())).c_str());
        ImGui::BeginDisabled(!valid);
        const bool ok = ImGui::Button("OK", ImVec2(100.0f, 0.0f)) || (entered && valid);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_nameAction = NameAction::None;
            ImGui::CloseCurrentPopup();
        }
        if (ok) {
            if (m_nameAction == NameAction::SavePose)
                savePose(name);
            else if (m_nameAction != NameAction::None)
                createClip(name, m_nameAction == NameAction::DuplicateClip);
            m_nameAction = NameAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Delete##poser", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete %s?", assetops::displayPath(m_deleteRef).c_str());
        ImGui::TextDisabled("This can't be undone. Scenes using it fall back to the bind pose.");
        if (ImGui::Button("Delete", ImVec2(100.0f, 0.0f))) {
            deleteClip(m_deleteRef);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void AnimationViewer::handleShortcuts()
{
    if (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive() ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || m_gizmoDragging)
        return;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
        EditContext edit{*m_scene, *m_components};
        m_history.undo(edit);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
        EditContext edit{*m_scene, *m_components};
        m_history.redo(edit);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_closeRequested = true;
        return;
    }
    if (m_posing && (m_view.hovered() || m_view.focused())) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false))
            m_gizmoOp = GizmoOp::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false))
            m_gizmoOp = GizmoOp::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false))
            m_gizmoOp = GizmoOp::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_X, false))
            m_gizmoLocal = !m_gizmoLocal;
    }
    if (m_posing && m_authored && ImGui::IsKeyPressed(ImGuiKey_K, false))
        keyPose();
    if (ImGui::IsKeyPressed(ImGuiKey_F, false))
        frameActor();
    if (!m_clipData)
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        if (!m_playing && !m_loop && currentFrame() >= m_out)
            m_time = frameTime(m_in);
        m_playing = !m_playing;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        stepFrame(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        stepFrame(1);
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
        stepFrame(m_in - currentFrame());
    if (ImGui::IsKeyPressed(ImGuiKey_End, false))
        stepFrame(m_out - currentFrame());
}

// POSER

AnimationViewer::PoserState AnimationViewer::state() const
{
    return {m_overrides, m_authored ? clipRef() : std::string(), m_authored};
}

void AnimationViewer::restore(const PoserState& state)
{
    m_overrides = state.overrides;
    if (!state.clip || state.clipRef.empty())
        return;
    const std::shared_ptr<const ClipData> baked = writeAuthored(state.clipRef, *state.clip);
    if (clipRef() == state.clipRef) {
        const int oldLast = lastFrame();
        m_authored = state.clip;
        m_clipData = baked;
        if (m_out == oldLast || m_out > lastFrame())
            m_out = lastFrame();
        m_in = std::min(m_in, m_out);
    }
}

void AnimationViewer::record(std::string name, PoserState before)
{
    m_status.clear();
    m_history.push(std::make_unique<PoserCommand>(*this, std::move(name), std::move(before), state()));
}

JointTrs AnimationViewer::displayedLocal(int joint) const
{
    if (const auto it = m_overrides.find(joint); m_posing && it != m_overrides.end())
        return it->second;
    const auto j = static_cast<std::size_t>(joint);
    const Animator& animator = m_scene->registry().get<Animator>(m_actor);
    if (j < animator.pose.t.size())
        return {animator.pose.t[j], animator.pose.r[j], animator.pose.s[j]};
    return m_skeleton->bindLocal[j];
}

Affine AnimationViewer::jointModel(int joint) const
{
    const SkinnedMeshRenderer& skinned = m_scene->registry().get<SkinnedMeshRenderer>(m_actor);
    const int parent = m_skeleton->parents[static_cast<std::size_t>(joint)];
    const Affine parentModel = parent >= 0 && static_cast<std::size_t>(parent) < skinned.jointModel.size()
                                   ? skinned.jointModel[static_cast<std::size_t>(parent)]
                                   : Affine{};
    const JointTrs local = displayedLocal(joint);
    return parentModel * Affine::fromTrs(local.t, local.r, local.s);
}

// ImGuizmo on the selected joint, or on a pole handle. Moving an IK effector solves its limb;
// anything else is forward kinematics on the joint's local transform under its parent.
// Planted and pinned limbs are solved back to their targets after every change. The drag
// is one undo step.
void AnimationViewer::drawJointGizmo()
{
    m_gizmoHovered = false;
    const bool usable = m_posing && m_skeleton && (m_joint >= 0 || m_poleLimb >= 0);
    if (m_gizmoDragging && (!ImGuizmo::IsUsing() || !usable)) {
        m_gizmoDragging = false;
        m_planted.clear();
        record(m_dragName, std::move(m_dragStart));
    }
    if (!usable)
        return;

    Pose local;
    std::vector<Affine> model;
    evaluate(local, model);
    const glm::mat4 actorWorld = m_scene->registry().get<WorldTransform>(m_actor).matrix;
    const bool pole = m_poleLimb >= 0;
    const int limb = !pole && m_ik ? effectorLimb(m_joint) : -1;
    const bool solving = limb >= 0 && m_gizmoOp == GizmoOp::Move;

    glm::mat4 world = pole ? actorWorld * glm::translate(glm::mat4(1.0f), poleFor(static_cast<std::size_t>(m_poleLimb), model))
                           : actorWorld * model[static_cast<std::size_t>(m_joint)].toMat4();
    const glm::mat4 view = viewMatrix();
    const glm::mat4 proj = projection();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(m_view.imageX(), m_view.imageY(), m_view.imageWidth(), m_view.imageHeight());
    const GizmoOp gizmoOp = pole ? GizmoOp::Move : m_gizmoOp;
    const ImGuizmo::OPERATION op = gizmoOp == GizmoOp::Move     ? ImGuizmo::TRANSLATE
                                   : gizmoOp == GizmoOp::Rotate ? ImGuizmo::ROTATE
                                                                : ImGuizmo::SCALE;
    const ImGuizmo::MODE mode = pole || solving                                  ? ImGuizmo::WORLD
                                : m_gizmoLocal || gizmoOp == GizmoOp::Scale ? ImGuizmo::LOCAL
                                                                             : ImGuizmo::WORLD;
    const float snapValue = gizmoOp == GizmoOp::Rotate ? 15.0f : gizmoOp == GizmoOp::Scale ? 0.1f : 0.05f;
    const float snap[3] = {snapValue, snapValue, snapValue};
    ImGuizmo::PushID(pole ? 1 : 0);
    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, mode, glm::value_ptr(world),
                                              nullptr, ImGui::GetIO().KeyCtrl ? snap : nullptr);
    ImGuizmo::PopID();
    m_gizmoHovered = ImGuizmo::IsOver() || ImGuizmo::IsUsing();

    if (ImGuizmo::IsUsing() && !m_gizmoDragging) {
        m_dragStart = state();
        m_gizmoDragging = true;
        m_playing = false;
        if (pole) {
            const IkLimb& l = m_rig.limbs[static_cast<std::size_t>(m_poleLimb)];
            m_dragEffector = model[static_cast<std::size_t>(effectorJoint(l))].translation();
            m_dragEndRotation = modelRotation(model[static_cast<std::size_t>(l.end)]);
            m_dragName = "Move " + l.label + " Pole";
            capturePlanted(-1, m_poleLimb);
        } else {
            const IkLimb* l = limb >= 0 ? &m_rig.limbs[static_cast<std::size_t>(limb)] : nullptr;
            m_dragEndRotation = modelRotation(model[static_cast<std::size_t>(l ? l->end : m_joint)]);
            m_dragPole = l ? poleFor(static_cast<std::size_t>(limb), model) : glm::vec3(0.0f);
            m_dragName = solving ? "Move " + l->label : "Pose " + m_skeleton->names[static_cast<std::size_t>(m_joint)];
            capturePlanted(m_joint, solving ? limb : -1);
        }
    }
    if (!changed || !m_gizmoDragging)
        return;

    const glm::mat4 inModel = glm::inverse(actorWorld) * world;
    if (pole) {
        const auto l = static_cast<std::size_t>(m_poleLimb);
        m_poles[l] = glm::vec3(inModel[3]);
        moveEffector(l, m_dragEffector, m_dragEndRotation, *m_poles[l]);
    } else if (solving) {
        const auto l = static_cast<std::size_t>(limb);
        moveEffector(l, glm::vec3(inModel[3]), m_dragEndRotation, m_poles[l] ? *m_poles[l] : m_dragPole);
    } else {
        const auto j = static_cast<std::size_t>(m_joint);
        const int parent = m_skeleton->parents[j];
        const glm::mat4 parentModel = parent >= 0 ? model[static_cast<std::size_t>(parent)].toMat4() : glm::mat4(1.0f);
        JointTrs next = decompose(Affine::fromMat4(glm::inverse(parentModel) * inModel));
        // Only the operated channel changes, so the others don't drift through the decomposition
        const JointTrs previous{local.t[j], local.r[j], local.s[j]};
        if (m_gizmoOp != GizmoOp::Move)
            next.t = previous.t;
        if (m_gizmoOp != GizmoOp::Rotate)
            next.r = previous.r;
        if (m_gizmoOp != GizmoOp::Scale)
            next.s = previous.s;
        m_overrides[m_joint] = next;
    }
    applyPlanted();
}

// IK

void AnimationViewer::evaluate(Pose& local, std::vector<Affine>& model) const
{
    local.setBind(*m_skeleton);
    if (m_clipData && !(m_posing && m_baseBind)) {
        if (m_baseBindingFor != m_clipData) {
            m_baseBinding = bindClip(*m_clipData, *m_skeleton);
            m_baseBindingFor = m_clipData;
        }
        sampleClip(*m_clipData, m_baseBinding, m_time, local);
    }
    if (m_posing) {
        for (const auto& [j, trs] : m_overrides) {
            const auto i = static_cast<std::size_t>(j);
            local.t[i] = trs.t;
            local.r[i] = trs.r;
            local.s[i] = trs.s;
        }
    }
    model.resize(m_skeleton->jointCount());
    localToModel(*m_skeleton, local, model);
}

int AnimationViewer::effectorLimb(int joint) const
{
    for (std::size_t i = 0; i < m_rig.limbs.size(); ++i) {
        if (joint >= 0 && effectorJoint(m_rig.limbs[i]) == joint)
            return static_cast<int>(i);
    }
    return -1;
}

bool AnimationViewer::limbContains(const IkLimb& limb, int joint) const
{
    if (joint == limb.control)
        return true;
    for (int j = limb.end; j >= 0; j = m_skeleton->parents[static_cast<std::size_t>(j)]) {
        if (j == joint)
            return true;
        if (j == limb.root)
            break;
    }
    return false;
}

// The user's pole, the rig's pole joint, or out from the knee / elbow as the pose bends it
glm::vec3 AnimationViewer::poleFor(std::size_t limb, const std::vector<Affine>& model) const
{
    if (m_poles[limb])
        return *m_poles[limb];
    const IkLimb& l = m_rig.limbs[limb];
    if (l.pole >= 0)
        return model[static_cast<std::size_t>(l.pole)].translation();
    const glm::vec3 a = model[static_cast<std::size_t>(l.root)].translation();
    const glm::vec3 b = model[static_cast<std::size_t>(l.mid)].translation();
    const glm::vec3 c = model[static_cast<std::size_t>(l.end)].translation();
    const float length = glm::length(b - a) + glm::length(c - b);
    const glm::vec3 line = glm::length(c - a) > 1e-6f ? glm::normalize(c - a) : glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 out = (b - a) - line * glm::dot(b - a, line);
    if (glm::length(out) < 1e-3f * length) {
        // Straight: knees forward, elbows back, by the way the rig faces
        out = l.kind == IkLimb::Kind::Leg ? m_rig.forward : -m_rig.forward;
        out -= line * glm::dot(out, line);
    }
    return b + glm::normalize(out) * std::max(length, 1e-3f);
}

void AnimationViewer::setJointPosition(int joint, const glm::vec3& modelPosition)
{
    Pose local;
    std::vector<Affine> model;
    evaluate(local, model);
    const auto j = static_cast<std::size_t>(joint);
    const int parent = m_skeleton->parents[j];
    const glm::mat4 parentModel = parent >= 0 ? model[static_cast<std::size_t>(parent)].toMat4() : glm::mat4(1.0f);
    m_overrides[joint] = {glm::vec3(glm::inverse(parentModel) * glm::vec4(modelPosition, 1.0f)), local.r[j], local.s[j]};
}

void AnimationViewer::moveEffector(std::size_t limb, const glm::vec3& target, const glm::quat& endRotation,
                                   const glm::vec3& pole)
{
    const IkLimb& l = m_rig.limbs[limb];
    // A rig's own foot / hand control moves; the chain's end follows it
    if (l.control >= 0)
        setJointPosition(l.control, target);
    Pose local;
    std::vector<Affine> model;
    evaluate(local, model);
    const glm::vec3 chainTarget =
        l.control >= 0 ? model[static_cast<std::size_t>(l.control)].transformPoint(l.controlOffset) : target;
    const bool holdEnd = l.control < 0 && l.keepEndRotation;
    solveTwoBoneIk(*m_skeleton, local, model, l.root, l.mid, l.end, chainTarget, pole, holdEnd ? &endRotation : nullptr);
    for (const int j : {l.root, l.mid, l.end}) {
        if (j == l.end && !holdEnd)
            continue;
        const auto i = static_cast<std::size_t>(j);
        m_overrides[j] = {local.t[i], local.r[i], local.s[i]};
    }
}

// The limbs that should hold still while joint moves: legs when Plant Feet is on, and every
// pinned limb, unless the moving joint is part of it
void AnimationViewer::capturePlanted(int draggedJoint, int draggedLimb)
{
    m_planted.clear();
    Pose local;
    std::vector<Affine> model;
    evaluate(local, model);
    for (std::size_t i = 0; i < m_rig.limbs.size(); ++i) {
        const IkLimb& l = m_rig.limbs[i];
        if (static_cast<int>(i) == draggedLimb || (draggedJoint >= 0 && limbContains(l, draggedJoint)))
            continue;
        const Pin& pin = m_pins[i];
        if (!pin.on && !(m_plantFeet && l.kind == IkLimb::Kind::Leg))
            continue;
        m_planted.push_back({i, pin.on ? pin.target : model[static_cast<std::size_t>(effectorJoint(l))].translation(),
                             pin.on ? pin.rotation : modelRotation(model[static_cast<std::size_t>(l.end)]), poleFor(i, model)});
    }
}

void AnimationViewer::applyPlanted()
{
    for (const Planted& p : m_planted)
        moveEffector(p.limb, p.target, p.rotation, m_poles[p.limb] ? *m_poles[p.limb] : p.pole);
}

void AnimationViewer::applyPins()
{
    if (!m_ik)
        return;
    for (std::size_t i = 0; i < m_pins.size(); ++i) {
        if (!m_pins[i].on)
            continue;
        Pose local;
        std::vector<Affine> model;
        evaluate(local, model);
        moveEffector(i, m_pins[i].target, m_pins[i].rotation, poleFor(i, model));
    }
}

void AnimationViewer::setPin(std::size_t limb, bool on)
{
    Pin& pin = m_pins[limb];
    pin.on = on;
    if (!on)
        return;
    Pose local;
    std::vector<Affine> model;
    evaluate(local, model);
    const IkLimb& l = m_rig.limbs[limb];
    pin.target = model[static_cast<std::size_t>(effectorJoint(l))].translation();
    pin.rotation = modelRotation(model[static_cast<std::size_t>(l.end)]);
}

// A diamond on every hand and foot effector, a square on the hips, a ring on every pole
void AnimationViewer::drawIkHandles(ImDrawList* dl, const glm::mat4& viewProj, const glm::vec2& origin,
                                    const glm::vec2& size)
{
    Pose local;
    std::vector<Affine> model;
    evaluate(local, model);
    const glm::mat4& world = m_scene->registry().get<WorldTransform>(m_actor).matrix;
    const Projector p{viewProj, origin, size};
    const float r = ImGui::GetFontSize() * 0.5f;
    auto project = [&](const glm::vec3& modelPoint, ImVec2& screen, float& depth) {
        const glm::vec4 clip = viewProj * world * glm::vec4(modelPoint, 1.0f);
        if (clip.w < MinW)
            return false;
        screen = p.toScreen(clip);
        depth = clip.w;
        return true;
    };
    const ImU32 effectorColor = IM_COL32(230, 80, 230, 255);
    const ImU32 pinnedColor = IM_COL32(240, 70, 70, 255);
    const ImU32 poleColor = IM_COL32(120, 255, 160, 255);
    for (std::size_t i = 0; i < m_rig.limbs.size(); ++i) {
        const IkLimb& l = m_rig.limbs[i];
        ImVec2 s;
        float depth;
        const glm::vec3 polePoint = poleFor(i, model);
        if (project(polePoint, s, depth)) {
            ImVec2 knee;
            float kneeDepth;
            if (project(model[static_cast<std::size_t>(l.mid)].translation(), knee, kneeDepth))
                dl->AddLine(knee, s, poleColor & IM_COL32(255, 255, 255, 110), 1.0f);
            const bool selected = m_poleLimb == static_cast<int>(i);
            dl->AddCircle(s, r * 0.6f, selected ? SelectedColor : poleColor, 0, selected ? 3.0f : 2.0f);
            m_handles.push_back({HandleKind::Pole, static_cast<int>(i), glm::vec2(s.x, s.y), depth});
        }
        if (project(model[static_cast<std::size_t>(effectorJoint(l))].translation(), s, depth)) {
            const bool selected = m_poleLimb < 0 && m_joint == effectorJoint(l);
            const ImU32 color = selected ? SelectedColor : m_pins[i].on ? pinnedColor : effectorColor;
            const ImVec2 pts[4] = {{s.x, s.y - r}, {s.x + r, s.y}, {s.x, s.y + r}, {s.x - r, s.y}};
            dl->AddQuadFilled(pts[0], pts[1], pts[2], pts[3], color & IM_COL32(255, 255, 255, 90));
            dl->AddQuad(pts[0], pts[1], pts[2], pts[3], color, 2.0f);
            m_handles.push_back({HandleKind::Effector, static_cast<int>(i), glm::vec2(s.x, s.y), depth});
        }
    }
    if (m_rig.hips >= 0) {
        ImVec2 s;
        float depth;
        if (project(model[static_cast<std::size_t>(m_rig.hips)].translation(), s, depth)) {
            const bool selected = m_poleLimb < 0 && m_joint == m_rig.hips;
            const ImU32 color = selected ? SelectedColor : IM_COL32(255, 210, 60, 255);
            dl->AddRectFilled(ImVec2(s.x - r, s.y - r), ImVec2(s.x + r, s.y + r), color & IM_COL32(255, 255, 255, 80));
            dl->AddRect(ImVec2(s.x - r, s.y - r), ImVec2(s.x + r, s.y + r), color, 0.0f, 0, 2.0f);
            m_handles.push_back({HandleKind::Hips, m_rig.hips, glm::vec2(s.x, s.y), depth});
        }
    }
}

void AnimationViewer::drawIkPanel()
{
    ImGui::SeparatorText("IK");
    if (m_rig.limbs.empty()) {
        ImGui::TextDisabled("No humanoid limbs found in this rig: forward kinematics only");
        return;
    }
    ImGui::Checkbox("IK", &m_ik);
    ImGui::SetItemTooltip("Hands and feet are effectors: drag their diamonds to solve the limb");
    ImGui::SameLine();
    ImGui::Checkbox("Plant feet", &m_plantFeet);
    ImGui::SetItemTooltip("Moving or turning the body keeps the feet where they are");
    ImGui::BeginDisabled(!m_ik);
    for (std::size_t i = 0; i < m_rig.limbs.size(); ++i) {
        const IkLimb& l = m_rig.limbs[i];
        ImGui::PushID(static_cast<int>(i));
        bool pinned = m_pins[i].on;
        if (ImGui::Checkbox("##pin", &pinned))
            setPin(i, pinned);
        ImGui::SetItemTooltip("Pin: the %s stays where it is now", l.kind == IkLimb::Kind::Leg ? "foot" : "hand");
        ImGui::SameLine();
        if (ImGui::Selectable(l.label.c_str(), m_poleLimb < 0 && m_joint == effectorJoint(l), 0,
                              ImVec2(ImGui::CalcTextSize("Right Arm").x + 8.0f, 0.0f))) {
            m_joint = effectorJoint(l);
            m_poleLimb = -1;
            m_gizmoOp = GizmoOp::Move;
            m_revealJoint = true;
        }
        ImGui::SetItemTooltip("%s > %s > %s%s%s", m_skeleton->names[static_cast<std::size_t>(l.root)].c_str(),
                              m_skeleton->names[static_cast<std::size_t>(l.mid)].c_str(),
                              m_skeleton->names[static_cast<std::size_t>(l.end)].c_str(),
                              l.control >= 0 ? ", driven by " : "",
                              l.control >= 0 ? m_skeleton->names[static_cast<std::size_t>(l.control)].c_str() : "");
        ImGui::SameLine();
        if (ImGui::SmallButton("Pole"))
            m_poleLimb = static_cast<int>(i);
        ImGui::SetItemTooltip("Select the pole: where the %s points", l.kind == IkLimb::Kind::Leg ? "knee" : "elbow");
        if (m_poles[i]) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset Pole"))
                m_poles[i].reset();
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (m_rig.hips >= 0) {
        if (ImGui::SmallButton("Select Hips")) {
            m_joint = m_rig.hips;
            m_poleLimb = -1;
            m_gizmoOp = GizmoOp::Move;
            m_revealJoint = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_skeleton->names[static_cast<std::size_t>(m_rig.hips)].c_str());
    }
}

void AnimationViewer::keyPose()
{
    if (!m_authored) {
        m_status = "Key Pose writes into an .anim clip: create one with New or Duplicate";
        return;
    }
    std::map<int, JointTrs> keyed = m_overrides;
    if (keyed.empty() && m_joint >= 0)
        keyed[m_joint] = displayedLocal(m_joint);
    if (keyed.empty())
        return;
    PoserState before = state();
    const int frame = currentFrame();
    for (const auto& [j, local] : keyed) {
        AuthoredTrack& track = m_authored->addTrack(m_skeleton->names[static_cast<std::size_t>(j)]);
        setKey(track.t, frame, local.t);
        setKey(track.r, frame, local.r);
        setKey(track.s, frame, local.s);
        m_overrides.erase(j);  // the clip frame now holds it
    }
    commitAuthored();
    record("Key Pose", std::move(before));
    m_status = "Keyed " + std::to_string(keyed.size()) + (keyed.size() == 1 ? " joint" : " joints") + " at frame " +
               std::to_string(frame);
}

void AnimationViewer::deleteKey()
{
    if (!m_authored || m_joint < 0)
        return;
    AuthoredTrack* track = m_authored->track(m_skeleton->names[static_cast<std::size_t>(m_joint)]);
    if (!track)
        return;
    const int frame = currentFrame();
    PoserState before = state();
    const std::size_t count = track->t.size() + track->r.size() + track->s.size();
    auto drop = [frame](auto& keys) { std::erase_if(keys, [frame](const auto& k) { return k.frame == frame; }); };
    drop(track->t);
    drop(track->r);
    drop(track->s);
    if (track->t.size() + track->r.size() + track->s.size() == count) {
        m_status = "No key at frame " + std::to_string(frame);
        return;
    }
    std::erase_if(m_authored->tracks, [](const AuthoredTrack& t) { return t.empty(); });
    commitAuthored();
    record("Delete Key", std::move(before));
}

// Every posed joint goes to its other side (itself if it has none), mirrored
void AnimationViewer::flipPose()
{
    PoserState before = state();
    std::map<int, JointTrs> flipped;
    for (const auto& [j, local] : m_overrides) {
        const std::string other = mirroredJointName(m_skeleton->names[static_cast<std::size_t>(j)]);
        int k = other.empty() ? -1 : m_skeleton->findJoint(other);
        if (k < 0)
            k = j;
        flipped[k] = mirrorLocal(local, m_skeleton->bindLocal[static_cast<std::size_t>(j)],
                                 m_skeleton->bindLocal[static_cast<std::size_t>(k)]);
    }
    m_overrides = std::move(flipped);
    record("Flip Pose", std::move(before));
}

void AnimationViewer::mirrorJoint()
{
    const std::string& name = m_skeleton->names[static_cast<std::size_t>(m_joint)];
    const std::string other = mirroredJointName(name);
    const int k = other.empty() ? -1 : m_skeleton->findJoint(other);
    if (k < 0) {
        m_status = name + " has no joint on the other side";
        return;
    }
    PoserState before = state();
    m_overrides[k] = mirrorLocal(displayedLocal(m_joint), m_skeleton->bindLocal[static_cast<std::size_t>(m_joint)],
                                 m_skeleton->bindLocal[static_cast<std::size_t>(k)]);
    record("Mirror " + name, std::move(before));
}

void AnimationViewer::applyPoseAsset(const PoseAsset& pose, std::string name)
{
    PoserState before = state();
    m_overrides.clear();
    std::size_t missing = 0;
    for (const PoseJoint& joint : pose.joints) {
        if (const int j = m_skeleton->findJoint(joint.joint); j >= 0)
            m_overrides[j] = joint.local;
        else
            ++missing;
    }
    m_posing = true;
    m_playing = false;
    record("Apply Pose " + name, std::move(before));
    if (pose.skeletonHash != m_skeletonHash)
        m_status = "This pose was made for another skeleton; joints matched by name";
    if (missing > 0)
        m_status = std::to_string(missing) + " joints of the pose aren't in this skeleton";
}

void AnimationViewer::openAsset(AssetCache& assets, const std::string& ref)
{
    if (!isOpen())
        return;
    if (assetKindOf(ref) == AssetKind::Pose) {
        std::string error;
        const auto text = assetops::readText(assetops::fromRef(ref));
        const std::optional<PoseAsset> pose = text ? loadPoseText(*text, &error) : std::nullopt;
        if (pose)
            applyPoseAsset(*pose, clipName(ref));
        else
            m_status = "Can't read " + assetops::displayPath(ref) + (error.empty() ? "" : ": " + error);
        return;
    }
    refreshAssets();
    const auto it = std::find(m_clips.begin(), m_clips.end(), ref);
    if (it != m_clips.end())
        selectClip(assets, static_cast<int>(it - m_clips.begin()));
    else
        m_status = assetops::displayPath(ref) + " wasn't made for this skeleton";
}

void AnimationViewer::savePose(const std::string& name)
{
    if (m_overrides.empty()) {
        m_status = "Nothing is posed";
        return;
    }
    PoseAsset pose{m_skeletonHash, m_model, {}};
    for (const auto& [j, local] : m_overrides)
        pose.joints.push_back({m_skeleton->names[static_cast<std::size_t>(j)], local});
    const std::filesystem::path path = assetops::uniquePath(assetFolder(), name, ".pose");
    std::string error;
    if (!assetops::writeText(path, savePoseText(pose) + "\n", &error)) {
        m_status = "Can't save the pose: " + error;
        return;
    }
    m_status = "Saved " + assetops::displayPath(assetops::toRef(path));
    m_assetsChanged = true;
    refreshAssets();
}

void AnimationViewer::createClip(const std::string& name, bool duplicate)
{
    AuthoredClip clip;
    if (duplicate && m_authored)
        clip = *m_authored;
    else if (duplicate && m_clipData)
        clip = unbakeClip(*m_clipData, *m_skeleton, m_model);
    else if (duplicate)
        return;
    clip.skeletonHash = m_skeletonHash;
    clip.model = m_model;
    const std::filesystem::path path = assetops::uniquePath(assetFolder(), name, ".anim");
    const std::string ref = assetops::toRef(path);
    if (!writeAuthored(ref, clip))
        return;
    m_assetsChanged = true;
    refreshAssets();
    if (const auto it = std::find(m_clips.begin(), m_clips.end(), ref); it != m_clips.end())
        selectClip(*m_assets, static_cast<int>(it - m_clips.begin()));
    m_playing = false;
    m_status = "Created " + assetops::displayPath(ref);
}

void AnimationViewer::deleteClip(const std::string& ref)
{
    std::string error;
    if (!assetops::remove(assetops::fromRef(ref), &error)) {
        m_status = "Can't delete: " + error;
        return;
    }
    if (assetKindOf(ref) == AssetKind::Animation) {
        m_assets->setClip(ref, nullptr);
        m_history.clear();  // its steps would write the file back
        const bool selected = clipRef() == ref;
        if (selected)
            selectClip(*m_assets, m_embeddedClips > 0 ? 0 : -1);
    }
    m_assetsChanged = true;
    refreshAssets();
    m_status = "Deleted " + assetops::displayPath(ref);
}

std::shared_ptr<const ClipData> AnimationViewer::writeAuthored(const std::string& ref, const AuthoredClip& clip)
{
    auto baked = std::make_shared<const ClipData>(bakeClip(clip, *m_skeleton, std::filesystem::path(ref).stem().string()));
    std::string error;
    if (!assetops::writeText(assetops::fromRef(ref), saveAnimText(clip, *baked) + "\n", &error)) {
        m_status = "Can't save " + assetops::displayPath(ref) + ": " + error;
        return nullptr;
    }
    m_assets->setClip(ref, baked);
    return baked;
}

void AnimationViewer::commitAuthored()
{
    if (!m_authored || m_clip < 0)
        return;
    const int oldLast = lastFrame();
    if (std::shared_ptr<const ClipData> baked = writeAuthored(clipRef(), *m_authored))
        m_clipData = std::move(baked);
    if (m_out == oldLast || m_out > lastFrame())
        m_out = lastFrame();
    m_in = std::min(m_in, m_out);
    m_time = std::min(m_time, frameTime(lastFrame()));
}

// .anim and .pose files made for this skeleton, anywhere in the project
void AnimationViewer::refreshAssets()
{
    const std::string selected = clipRef();
    m_clips.resize(m_embeddedClips);
    m_poses.clear();
    for (const AssetKind kind : {AssetKind::Animation, AssetKind::Pose}) {
        for (const std::string& ref : assetops::findAssets(kind)) {
            const auto text = assetops::readText(assetops::fromRef(ref));
            const std::optional<AnimHeader> header = text ? readAnimHeader(*text) : std::nullopt;
            if (!header || header->skeletonHash != m_skeletonHash)
                continue;
            (kind == AssetKind::Animation ? m_clips : m_poses).push_back(ref);
        }
    }
    const auto it = std::find(m_clips.begin(), m_clips.end(), selected);
    m_clip = it == m_clips.end() ? (selected.empty() ? m_clip : -1) : static_cast<int>(it - m_clips.begin());
}

std::string AnimationViewer::assetFolder() const
{
    return assetops::fromRef(m_model).parent_path().string();
}
