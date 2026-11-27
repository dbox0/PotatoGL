#include "SceneGizmos.h"

#include "editor/Selection.h"

#include "engine/scene/Camera.h"
#include "engine/scene/Scene.h"

#include <imgui.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace
{
constexpr float MinW = 1e-3f;  // clip w below this is behind the camera
const ImU32 LightWire = IM_COL32(255, 235, 140, 220);
const ImU32 LightWireDim = IM_COL32(255, 235, 140, 110);
const ImU32 CameraWire = IM_COL32(220, 220, 220, 200);
const ImU32 IconBack = IM_COL32(25, 25, 25, 190);
const ImU32 IconEdge = IM_COL32(0, 0, 0, 220);
const ImU32 IconSelected = IM_COL32(255, 110, 20, 255);
const ImU32 CameraColor = IM_COL32(210, 210, 210, 255);

struct Painter
{
    ImDrawList* drawList;
    const SceneGizmos::View& view;

    ImVec2 toScreen(const glm::vec4& clip) const
    {
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        return ImVec2(view.origin.x + (ndc.x * 0.5f + 0.5f) * view.size.x,
                      view.origin.y + (0.5f - ndc.y * 0.5f) * view.size.y);
    }

    // Clipped to the near side, so lines through the camera don't wrap around
    void line(const glm::vec3& a, const glm::vec3& b, ImU32 color) const
    {
        glm::vec4 ca = view.viewProj * glm::vec4(a, 1.0f);
        glm::vec4 cb = view.viewProj * glm::vec4(b, 1.0f);
        if (ca.w < MinW && cb.w < MinW)
            return;
        if (ca.w < MinW)
            ca = glm::mix(ca, cb, (MinW - ca.w) / (cb.w - ca.w));
        else if (cb.w < MinW)
            cb = glm::mix(cb, ca, (MinW - cb.w) / (ca.w - cb.w));
        drawList->AddLine(toScreen(ca), toScreen(cb), color, 1.5f);
    }

    void circle(const glm::vec3& center, const glm::vec3& axisA, const glm::vec3& axisB, float radius,
                ImU32 color) const
    {
        constexpr int Segments = 48;
        glm::vec3 prev = center + axisA * radius;
        for (int i = 1; i <= Segments; ++i) {
            const float angle = glm::two_pi<float>() * static_cast<float>(i) / Segments;
            const glm::vec3 next = center + (axisA * std::cos(angle) + axisB * std::sin(angle)) * radius;
            line(prev, next, color);
            prev = next;
        }
    }
};

struct Frame
{
    glm::vec3 pos;
    glm::vec3 right;
    glm::vec3 up;
    glm::vec3 forward;  // local -Z
};

Frame frameOf(const glm::mat4& world)
{
    return {glm::vec3(world[3]), glm::normalize(glm::vec3(world[0])), glm::normalize(glm::vec3(world[1])),
            -glm::normalize(glm::vec3(world[2]))};
}

void drawDirectional(const Painter& p, const Frame& f)
{
    constexpr float Radius = 0.5f;
    constexpr float Length = 2.0f;
    p.circle(f.pos, f.right, f.up, Radius, LightWire);
    p.line(f.pos, f.pos + f.forward * Length, LightWire);
    for (int i = 0; i < 8; ++i) {
        const float angle = glm::two_pi<float>() * static_cast<float>(i) / 8.0f;
        const glm::vec3 start = f.pos + (f.right * std::cos(angle) + f.up * std::sin(angle)) * Radius;
        p.line(start, start + f.forward * Length, LightWire);
    }
}

void drawPoint(const Painter& p, const Frame& f, const PointLight& light)
{
    const glm::vec3 x(1.0f, 0.0f, 0.0f), y(0.0f, 1.0f, 0.0f), z(0.0f, 0.0f, 1.0f);
    p.circle(f.pos, x, y, light.range, LightWire);
    p.circle(f.pos, x, z, light.range, LightWire);
    p.circle(f.pos, y, z, light.range, LightWire);
}

// The cone's slant length is the range, like Unity's
void drawSpot(const Painter& p, const Frame& f, const SpotLight& light)
{
    auto cone = [&](float angleDeg, ImU32 color, bool edges) {
        const float angle = glm::radians(std::clamp(angleDeg, 0.0f, 89.0f));
        const glm::vec3 center = f.pos + f.forward * (light.range * std::cos(angle));
        const float radius = light.range * std::sin(angle);
        p.circle(center, f.right, f.up, radius, color);
        if (!edges)
            return;
        for (const glm::vec3& dir : {f.right, -f.right, f.up, -f.up})
            p.line(f.pos, center + dir * radius, color);
    };
    cone(light.innerDeg, LightWireDim, false);
    cone(light.outerDeg, LightWire, true);
}

void drawFrustum(const Painter& p, const glm::mat4& world, const CameraComponent& camera, float aspect)
{
    const glm::mat4 invViewProj = glm::inverse(camera.projection(aspect) * viewFromWorld(world));
    glm::vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        const glm::vec4 ndc((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f, 1.0f);
        const glm::vec4 w = invViewProj * ndc;
        corners[i] = glm::vec3(w) / w.w;
    }
    // Corners that differ in one bit share an edge
    for (int i = 0; i < 8; ++i) {
        for (const int bit : {1, 2, 4}) {
            if (!(i & bit))
                p.line(corners[i], corners[i | bit], CameraWire);
        }
    }
}

ImU32 iconColor(const glm::vec3& color)
{
    const float peak = std::max({color.r, color.g, color.b});
    const glm::vec3 c = peak > 1e-4f ? color / peak : glm::vec3(0.5f);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, 1.0f));
}

enum class IconKind
{
    Directional,
    Point,
    Spot,
    Camera,
};

void drawIcon(ImDrawList* drawList, ImVec2 c, float r, IconKind kind, ImU32 color, bool selected)
{
    drawList->AddCircleFilled(c, r, IconBack);
    switch (kind) {
    case IconKind::Directional:
        drawList->AddCircleFilled(c, r * 0.35f, color);
        for (int i = 0; i < 8; ++i) {
            const float angle = glm::two_pi<float>() * static_cast<float>(i) / 8.0f;
            const ImVec2 dir(std::cos(angle), std::sin(angle));
            drawList->AddLine(ImVec2(c.x + dir.x * r * 0.5f, c.y + dir.y * r * 0.5f),
                              ImVec2(c.x + dir.x * r * 0.8f, c.y + dir.y * r * 0.8f), color, 1.5f);
        }
        break;
    case IconKind::Point:
        drawList->AddCircleFilled(ImVec2(c.x, c.y - r * 0.1f), r * 0.45f, color);
        drawList->AddRectFilled(ImVec2(c.x - r * 0.2f, c.y + r * 0.3f), ImVec2(c.x + r * 0.2f, c.y + r * 0.6f),
                                color);
        break;
    case IconKind::Spot:
        drawList->AddTriangleFilled(ImVec2(c.x, c.y - r * 0.6f), ImVec2(c.x + r * 0.55f, c.y + r * 0.5f),
                                    ImVec2(c.x - r * 0.55f, c.y + r * 0.5f), color);
        break;
    case IconKind::Camera:
        drawList->AddRectFilled(ImVec2(c.x - r * 0.6f, c.y - r * 0.35f), ImVec2(c.x + r * 0.2f, c.y + r * 0.35f),
                                color, r * 0.1f);
        drawList->AddTriangleFilled(ImVec2(c.x + r * 0.2f, c.y), ImVec2(c.x + r * 0.65f, c.y - r * 0.35f),
                                    ImVec2(c.x + r * 0.65f, c.y + r * 0.35f), color);
        break;
    }
    drawList->AddCircle(c, r, selected ? IconSelected : IconEdge, 0, selected ? 2.0f : 1.0f);
}

float iconRadius()
{
    return ImGui::GetFontSize() * 0.7f;
}
}  // namespace

void SceneGizmos::draw(ImDrawList* drawList, const Scene& scene, const Selection& selection, const View& view)
{
    const entt::registry& registry = scene.registry();
    const Painter painter{drawList, view};
    m_icons.clear();

    for (const entt::entity e : selection.items()) {
        if (!registry.valid(e) || !registry.all_of<WorldTransform>(e))
            continue;
        const glm::mat4& world = registry.get<WorldTransform>(e).matrix;
        const Frame frame = frameOf(world);
        if (registry.all_of<DirectionalLight>(e))
            drawDirectional(painter, frame);
        if (const PointLight* light = registry.try_get<PointLight>(e))
            drawPoint(painter, frame, *light);
        if (const SpotLight* light = registry.try_get<SpotLight>(e))
            drawSpot(painter, frame, *light);
        if (const CameraComponent* camera = registry.try_get<CameraComponent>(e))
            drawFrustum(painter, world, *camera, view.gameAspect);
    }

    const float radius = iconRadius();
    auto icon = [&](entt::entity e, const WorldTransform& world, IconKind kind, ImU32 color) {
        const glm::vec4 clip = view.viewProj * glm::vec4(glm::vec3(world.matrix[3]), 1.0f);
        if (clip.w < MinW)
            return;
        const ImVec2 screen = painter.toScreen(clip);
        if (screen.x < view.origin.x - radius || screen.y < view.origin.y - radius ||
            screen.x > view.origin.x + view.size.x + radius || screen.y > view.origin.y + view.size.y + radius)
            return;
        drawIcon(drawList, screen, radius, kind, color, selection.contains(e));
        m_icons.push_back({e, glm::vec2(screen.x, screen.y), clip.w});
    };
    for (const auto [e, world, light] : registry.view<WorldTransform, DirectionalLight>().each())
        icon(e, world, IconKind::Directional, iconColor(light.color));
    for (const auto [e, world, light] : registry.view<WorldTransform, PointLight>().each())
        icon(e, world, IconKind::Point, iconColor(light.color));
    for (const auto [e, world, light] : registry.view<WorldTransform, SpotLight>().each())
        icon(e, world, IconKind::Spot, iconColor(light.color));
    for (const auto [e, world, camera] : registry.view<WorldTransform, CameraComponent>().each())
        icon(e, world, IconKind::Camera, CameraColor);
}

entt::entity SceneGizmos::pick(const glm::vec2& mouse) const
{
    const float radius = iconRadius();
    entt::entity best = entt::null;
    float bestDepth = 0.0f;
    for (const Icon& icon : m_icons) {
        const glm::vec2 d = mouse - icon.pos;
        if (glm::dot(d, d) <= radius * radius && (best == entt::null || icon.depth < bestDepth)) {
            best = icon.entity;
            bestDepth = icon.depth;
        }
    }
    return best;
}
