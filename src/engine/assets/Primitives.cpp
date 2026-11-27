#include "Primitives.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
constexpr int Segments = 32;

void addQuad(MeshData& mesh, const glm::vec3& center, const glm::vec3& u, const glm::vec3& v)
{
    const glm::vec3 normal = glm::normalize(glm::cross(u, v));
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({center - u - v, normal, {0.0f, 0.0f}});
    mesh.vertices.push_back({center + u - v, normal, {1.0f, 0.0f}});
    mesh.vertices.push_back({center + u + v, normal, {1.0f, 1.0f}});
    mesh.vertices.push_back({center - u + v, normal, {0.0f, 1.0f}});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

// A point of a revolved profile: distance from the Y axis, height, and the normal in that plane
struct ProfilePoint
{
    float radius;
    float y;
    glm::vec2 normal;  // (radial, y)
    float v;
};

// Revolves the profile around +Y. The profile must run so that the surface faces outward:
// bottom to top on the side, center to rim on a bottom cap, rim to center on a top cap.
void lathe(MeshData& mesh, const std::vector<ProfilePoint>& profile)
{
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const ProfilePoint& p : profile) {
        for (int j = 0; j <= Segments; ++j) {
            const float u = static_cast<float>(j) / Segments;
            const float phi = u * glm::two_pi<float>();
            const glm::vec3 radial(std::sin(phi), 0.0f, std::cos(phi));
            const glm::vec3 normal = glm::normalize(radial * p.normal.x + glm::vec3(0.0f, p.normal.y, 0.0f));
            mesh.vertices.push_back({radial * p.radius + glm::vec3(0.0f, p.y, 0.0f), normal, {u, p.v}});
        }
    }
    const auto segments = static_cast<std::uint32_t>(Segments);
    const std::uint32_t row = segments + 1;
    for (std::uint32_t i = 0; i + 1 < profile.size(); ++i) {
        for (std::uint32_t j = 0; j < segments; ++j) {
            const std::uint32_t a = base + i * row + j;
            const std::uint32_t b = a + 1;
            const std::uint32_t c = b + row;
            const std::uint32_t d = a + row;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
        }
    }
}

// Half sphere rings from the pole to the equator (top = false: the bottom one)
void hemisphere(std::vector<ProfilePoint>& out, float radius, float centerY, bool top, int rings, float v0, float v1)
{
    for (int k = 0; k <= rings; ++k) {
        const float t = static_cast<float>(k) / rings;
        const float theta = (top ? 0.5f + 0.5f * t : 0.5f * t) * glm::pi<float>();  // 0 at the bottom pole
        const glm::vec2 n(std::sin(theta), -std::cos(theta));
        out.push_back({radius * n.x, centerY + radius * n.y, n, v0 + (v1 - v0) * t});
    }
}

MeshData cube()
{
    MeshData mesh;
    const glm::vec3 x(0.5f, 0.0f, 0.0f), y(0.0f, 0.5f, 0.0f), z(0.0f, 0.0f, 0.5f);
    addQuad(mesh, x, -z, y);
    addQuad(mesh, -x, z, y);
    addQuad(mesh, y, x, -z);
    addQuad(mesh, -y, x, z);
    addQuad(mesh, z, x, y);
    addQuad(mesh, -z, -x, y);
    return mesh;
}

MeshData sphere()
{
    std::vector<ProfilePoint> profile;
    hemisphere(profile, 0.5f, 0.0f, false, Segments / 4, 0.0f, 0.5f);
    profile.pop_back();
    hemisphere(profile, 0.5f, 0.0f, true, Segments / 4, 0.5f, 1.0f);
    MeshData mesh;
    lathe(mesh, profile);
    return mesh;
}

MeshData capsule()
{
    std::vector<ProfilePoint> profile;
    hemisphere(profile, 0.5f, -0.5f, false, Segments / 4, 0.0f, 0.25f);
    hemisphere(profile, 0.5f, 0.5f, true, Segments / 4, 0.75f, 1.0f);
    MeshData mesh;
    lathe(mesh, profile);
    return mesh;
}

MeshData cylinder()
{
    const glm::vec2 down(0.0f, -1.0f), side(1.0f, 0.0f), up(0.0f, 1.0f);
    MeshData mesh;
    lathe(mesh, {{0.0f, -1.0f, down, 0.0f}, {0.5f, -1.0f, down, 1.0f}});
    lathe(mesh, {{0.5f, -1.0f, side, 0.0f}, {0.5f, 1.0f, side, 1.0f}});
    lathe(mesh, {{0.5f, 1.0f, up, 0.0f}, {0.0f, 1.0f, up, 1.0f}});
    return mesh;
}

MeshData plane()
{
    MeshData mesh;
    addQuad(mesh, glm::vec3(0.0f), glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -5.0f));
    return mesh;
}

MeshData quad()
{
    MeshData mesh;
    addQuad(mesh, glm::vec3(0.0f), glm::vec3(0.5f, 0.0f, 0.0f), glm::vec3(0.0f, 0.5f, 0.0f));
    return mesh;
}
}  // namespace

std::optional<ModelData> makePrimitive(std::string_view name)
{
    MeshData mesh;
    if (name == "cube")
        mesh = cube();
    else if (name == "sphere")
        mesh = sphere();
    else if (name == "capsule")
        mesh = capsule();
    else if (name == "cylinder")
        mesh = cylinder();
    else if (name == "plane")
        mesh = plane();
    else if (name == "quad")
        mesh = quad();
    else
        return std::nullopt;
    ModelData model;
    model.meshes.push_back(std::move(mesh));
    return model;
}
