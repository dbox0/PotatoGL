#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Skins reference joints with u8 indices
inline constexpr std::size_t MaxSkinJoints = 256;

// Affine transform stored as its top three rows (last row 0 0 0 1), like DrawData and the
// GPU joint palette
struct Affine
{
    glm::vec4 rows[3]{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};

    static Affine fromMat4(const glm::mat4& m);
    // T * R * S
    static Affine fromTrs(const glm::vec3& t, const glm::quat& r, const glm::vec3& s);
    glm::mat4 toMat4() const;
    glm::vec3 translation() const { return {rows[0].w, rows[1].w, rows[2].w}; }
    glm::vec3 transformPoint(const glm::vec3& p) const
    {
        const glm::vec4 h(p, 1.0f);
        return {glm::dot(rows[0], h), glm::dot(rows[1], h), glm::dot(rows[2], h)};
    }
};

inline Affine operator*(const Affine& a, const Affine& b)
{
    Affine c;
    for (int i = 0; i < 3; ++i) {
        const glm::vec4& r = a.rows[i];
        c.rows[i] = r.x * b.rows[0] + r.y * b.rows[1] + r.z * b.rows[2] + glm::vec4(0.0f, 0.0f, 0.0f, r.w);
    }
    return c;
}

struct JointTrs
{
    glm::vec3 t{0.0f};
    glm::quat r = glm::identity<glm::quat>();
    glm::vec3 s{1.0f};
};

// FNV-1a; clips and poses bind to skeletons by joint name hash
std::uint32_t jointNameHash(std::string_view name);

// Shared and immutable once imported. Joints are sorted so a parent always comes before its children.
struct SkeletonData
{
    std::vector<std::int16_t> parents;  // -1 = root
    std::vector<Affine> inverseBind;
    std::vector<JointTrs> bindLocal;
    std::vector<Affine> bindModel;  // bindLocal composed down the hierarchy
    std::vector<std::string> names;
    std::vector<std::uint32_t> nameHashes;

    std::size_t jointCount() const { return parents.size(); }
    // -1 if there's no joint by that name
    int findJoint(std::string_view name) const;
};
