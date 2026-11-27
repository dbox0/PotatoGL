#include "AnimationViewer.h"

#include "engine/anim/Clip.h"
#include "engine/anim/Pose.h"
#include "engine/app/Overlay.h"
#include "engine/assets/AssetCache.h"
#include "engine/render/Renderer.h"
#include "engine/scene/Components.h"
#include "engine/scene/Scene.h"
#include "engine/scene/Transform.h"

#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>
#include <imgui_internal.h>

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

    m_clips = assets.clipRefs(model);
    const auto start = std::find(m_clips.begin(), m_clips.end(), clip);
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
    m_clip = -1;
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
    m_clipData = assets.clip(m_clips[static_cast<std::size_t>(index)]);
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
    const std::string ref = m_clip >= 0 ? m_clips[static_cast<std::size_t>(m_clip)] : std::string();
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
    drawDockSpace();
    advance(ImGui::GetIO().DeltaTime);
    drawViewport();
    drawClips(context.assets);
    drawTimeline();
    drawSkeleton();
    drawJoint();
    handleShortcuts();
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

        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::SetCursorScreenPos(ImVec2(m_view.imageX() + 8.0f, m_view.imageY() + 8.0f));
        ImGui::TextColored(ImVec4(0.45f, 0.7f, 1.0f, 1.0f), "%s", m_name.c_str());
        if (m_clip >= 0) {
            ImGui::SameLine();
            ImGui::TextUnformatted(clipName(m_clips[static_cast<std::size_t>(m_clip)]).c_str());
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
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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
        if (glm::length(mouse - m_pressPos) < 4.0f) {
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
    dl->PopClipRect();
}

void AnimationViewer::drawClips(AssetCache& assets)
{
    if (ImGui::Begin(ClipsWindow)) {
        ImGui::TextDisabled("%s", m_model.c_str());
        ImGui::SeparatorText("Embedded");
        if (m_clips.empty())
            ImGui::TextDisabled("This model has no clips");
        for (std::size_t i = 0; i < m_clips.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const std::shared_ptr<const ClipData> clip = assets.clip(m_clips[i]);
            if (ImGui::Selectable(clipName(m_clips[i]).c_str(), static_cast<int>(i) == m_clip))
                selectClip(assets, static_cast<int>(i));
            if (clip) {
                char length[32];
                std::snprintf(length, sizeof(length), "%.2f s", clip->duration);
                ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(length).x);
                ImGui::TextDisabled("%s", length);
            }
            ImGui::PopID();
        }
        ImGui::SeparatorText("Authored");
        ImGui::TextDisabled("Read-only for now: .anim clips come with the Poser");
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

    // Embedded clips have a key on every frame of an animated channel
    if (m_clipData && m_skeleton && m_joint >= 0) {
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
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        m_joint = joint;
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
    if (m_joint < 0 || !m_skeleton) {
        ImGui::TextDisabled("Select a joint in the viewport or the Skeleton panel");
        ImGui::End();
        return;
    }
    const auto j = static_cast<std::size_t>(m_joint);
    const entt::registry& registry = m_scene->registry();
    const Animator& animator = registry.get<Animator>(m_actor);
    const SkinnedMeshRenderer& skinned = registry.get<SkinnedMeshRenderer>(m_actor);

    ImGui::TextUnformatted(m_skeleton->names[j].c_str());
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

    ImGui::SeparatorText("Local");
    JointTrs local = m_skeleton->bindLocal[j];
    if (j < animator.pose.t.size()) {
        local.t = animator.pose.t[j];
        local.r = animator.pose.r[j];
        local.s = animator.pose.s[j];
    }
    glm::vec3 euler = glm::degrees(glm::eulerAngles(local.r));
    ImGui::PushItemWidth(-80.0f);
    ImGui::BeginDisabled();
    ImGui::DragFloat3("Position", &local.t.x);
    ImGui::DragFloat3("Rotation", &euler.x);
    ImGui::DragFloat3("Scale", &local.s.x);
    ImGui::EndDisabled();
    ImGui::SeparatorText("Model space");
    glm::vec3 model = j < skinned.jointModel.size() ? skinned.jointModel[j].translation() : glm::vec3(0.0f);
    ImGui::BeginDisabled();
    ImGui::DragFloat3("Position##model", &model.x);
    ImGui::EndDisabled();
    ImGui::PopItemWidth();
    ImGui::Spacing();
    ImGui::TextDisabled("Editing joints comes with the Poser");
    ImGui::End();
}

void AnimationViewer::handleShortcuts()
{
    if (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive())
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_closeRequested = true;
        return;
    }
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
    if (ImGui::IsKeyPressed(ImGuiKey_F, false))
        frameActor();
}
