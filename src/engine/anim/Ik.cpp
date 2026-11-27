#include "Ik.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace
{
enum class Side
{
    None,
    Left,
    Right,
};

std::string lowered(std::string_view name)
{
    std::string out(name);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

// "left"/"right" anywhere, or a token "l"/"r" between separators: Arm.L, thigh_l, Bip01 L Thigh
Side sideOf(std::string_view name)
{
    const std::string lower = lowered(name);
    if (lower.find("left") != std::string::npos)
        return Side::Left;
    if (lower.find("right") != std::string::npos)
        return Side::Right;
    std::size_t i = 0;
    while (i < lower.size()) {
        while (i < lower.size() && !std::isalnum(static_cast<unsigned char>(lower[i])))
            ++i;
        std::size_t j = i;
        while (j < lower.size() && std::isalnum(static_cast<unsigned char>(lower[j])))
            ++j;
        const std::string_view token = std::string_view(lower).substr(i, j - i);
        if (token == "l")
            return Side::Left;
        if (token == "r")
            return Side::Right;
        i = j;
    }
    return Side::None;
}

bool containsAny(const std::string& lower, std::initializer_list<const char*> words)
{
    for (const char* w : words) {
        if (lower.find(w) != std::string::npos)
            return true;
    }
    return false;
}

struct Analysis
{
    const SkeletonData& skeleton;
    std::vector<std::string> lower;
    std::vector<Side> side;
    std::vector<int> depth;
    std::vector<int> subtree;  // joints in the subtree, itself included

    explicit Analysis(const SkeletonData& s) : skeleton(s)
    {
        const std::size_t n = s.jointCount();
        lower.resize(n);
        side.resize(n);
        depth.assign(n, 0);
        subtree.assign(n, 1);
        for (std::size_t j = 0; j < n; ++j) {
            lower[j] = lowered(s.names[j]);
            side[j] = sideOf(s.names[j]);
            if (s.parents[j] >= 0)
                depth[j] = depth[static_cast<std::size_t>(s.parents[j])] + 1;
        }
        for (std::size_t j = n; j-- > 0;) {
            if (s.parents[j] >= 0)
                subtree[static_cast<std::size_t>(s.parents[j])] += subtree[j];
        }
    }

    bool isAncestor(int ancestor, int j) const
    {
        for (int p = skeleton.parents[static_cast<std::size_t>(j)]; p >= 0; p = skeleton.parents[static_cast<std::size_t>(p)]) {
            if (p == ancestor)
                return true;
        }
        return false;
    }

    // The shallowest joint passing test; ties go to the one with more joints below it
    template<typename Test>
    int shallowest(Test test) const
    {
        int best = -1;
        for (std::size_t j = 0; j < lower.size(); ++j) {
            if (!test(j))
                continue;
            const auto b = static_cast<std::size_t>(best);
            if (best < 0 || depth[j] < depth[b] || (depth[j] == depth[b] && subtree[j] > subtree[b]))
                best = static_cast<int>(j);
        }
        return best;
    }

    // From start down through the child with the most joints below it
    std::vector<int> mainPath(int start) const
    {
        std::vector<int> path{start};
        for (;;) {
            int next = -1;
            for (std::size_t c = 0; c < lower.size(); ++c) {
                if (skeleton.parents[c] == path.back() && (next < 0 || subtree[c] > subtree[static_cast<std::size_t>(next)]))
                    next = static_cast<int>(c);
            }
            if (next < 0)
                return path;
            path.push_back(next);
        }
    }

    // First joint of path after index from whose name has one of words; -1 if none
    int findAfter(const std::vector<int>& path, std::size_t from, std::initializer_list<const char*> words) const
    {
        for (std::size_t i = from + 1; i < path.size(); ++i) {
            if (containsAny(lower[static_cast<std::size_t>(path[i])], words))
                return static_cast<int>(i);
        }
        return -1;
    }
};

std::optional<IkLimb> findLimb(const Analysis& a, IkLimb::Kind kind, Side side)
{
    const bool leg = kind == IkLimb::Kind::Leg;
    const int start = a.shallowest([&](std::size_t j) {
        if (a.side[j] != side)
            return false;
        const std::string& n = a.lower[j];
        if (leg)
            return containsAny(n, {"leg", "thigh", "femur"}) && !containsAny(n, {"foot", "toe", "ankle"});
        return containsAny(n, {"arm", "humerus"}) && !containsAny(n, {"fore", "lower", "armature", "clav", "shoulder", "collar"});
    });
    if (start < 0)
        return std::nullopt;
    const std::vector<int> path = a.mainPath(start);
    if (path.size() < 3)
        return std::nullopt;

    int midIndex = leg ? a.findAfter(path, 0, {"calf", "shin", "knee", "lower", "leg"})
                       : a.findAfter(path, 0, {"fore", "lower", "elbow"});
    if (midIndex < 0 || midIndex >= static_cast<int>(path.size()) - 1)
        midIndex = 1;
    int endIndex = leg ? a.findAfter(path, static_cast<std::size_t>(midIndex), {"foot", "ankle"})
                       : a.findAfter(path, static_cast<std::size_t>(midIndex), {"hand", "wrist", "palm"});
    if (endIndex < 0)
        endIndex = midIndex + 1;

    IkLimb limb;
    limb.kind = kind;
    limb.left = side == Side::Left;
    limb.root = path[0];
    limb.mid = path[static_cast<std::size_t>(midIndex)];
    limb.end = path[static_cast<std::size_t>(endIndex)];
    limb.label = std::string(limb.left ? "Left " : "Right ") + (leg ? "Leg" : "Arm");

    // Siblings of the end on the way down that have joints of their own (other palms, a
    // thumb) mean there's no wrist; leaf siblings like twist bones don't count
    const int endParent = a.skeleton.parents[static_cast<std::size_t>(limb.end)];
    for (std::size_t j = 0; j < a.lower.size(); ++j) {
        if (a.skeleton.parents[j] == endParent && static_cast<int>(j) != limb.end && a.subtree[j] > 1)
            limb.keepEndRotation = false;
    }

    // A foot / hand of this side outside the chain drives it: the rig's own IK control
    const bool endNamed = containsAny(a.lower[static_cast<std::size_t>(limb.end)],
                                      leg ? std::initializer_list<const char*>{"foot", "ankle"}
                                          : std::initializer_list<const char*>{"hand", "wrist", "palm"});
    if (!endNamed) {
        limb.control = a.shallowest([&](std::size_t j) {
            const int jj = static_cast<int>(j);
            return a.side[j] == side && jj != limb.root && !a.isAncestor(limb.root, jj) && !a.isAncestor(jj, limb.root) &&
                   containsAny(a.lower[j], leg ? std::initializer_list<const char*>{"foot", "ankle"}
                                               : std::initializer_list<const char*>{"hand", "wrist"});
        });
        if (limb.control >= 0) {
            const SkeletonData& s = a.skeleton;
            const glm::mat4 inverse = glm::inverse(s.bindModel[static_cast<std::size_t>(limb.control)].toMat4());
            limb.controlOffset = glm::vec3(inverse * glm::vec4(s.bindModel[static_cast<std::size_t>(limb.end)].translation(), 1.0f));
        }
    }
    limb.pole = a.shallowest([&](std::size_t j) {
        const std::string& n = a.lower[j];
        if (a.side[j] != side || n.find("pole") == std::string::npos)
            return false;
        const bool armPole = containsAny(n, {"arm", "elbow", "hand"});
        return leg ? !armPole : armPole;
    });
    return limb;
}

glm::mat4 aboutPoint(const glm::vec3& point, const glm::quat& rotation)
{
    return glm::translate(glm::mat4(1.0f), point) * glm::mat4_cast(rotation) * glm::translate(glm::mat4(1.0f), -point);
}

// Gives joint the rotation that puts it at newModel under its parent; position and scale stay
void setLocalRotation(const SkeletonData& skeleton, Pose& local, const std::vector<Affine>& model, int joint,
                      const glm::mat4& newModel)
{
    const int parent = skeleton.parents[static_cast<std::size_t>(joint)];
    const glm::mat4 parentModel = parent >= 0 ? model[static_cast<std::size_t>(parent)].toMat4() : glm::mat4(1.0f);
    local.r[static_cast<std::size_t>(joint)] = decompose(Affine::fromMat4(glm::inverse(parentModel) * newModel)).r;
}

float angleBetween(const glm::vec3& u, const glm::vec3& v)
{
    return std::acos(std::clamp(glm::dot(glm::normalize(u), glm::normalize(v)), -1.0f, 1.0f));
}
}  // namespace

HumanoidRig detectHumanoid(const SkeletonData& skeleton)
{
    HumanoidRig rig;
    const Analysis analysis(skeleton);
    for (const IkLimb::Kind kind : {IkLimb::Kind::Arm, IkLimb::Kind::Leg}) {
        for (const Side side : {Side::Left, Side::Right}) {
            if (std::optional<IkLimb> limb = findLimb(analysis, kind, side))
                rig.limbs.push_back(std::move(*limb));
        }
    }
    if (rig.limbs.size() < 2)
        return rig;

    // The deepest joint every limb hangs from
    int hips = rig.limbs.front().root;
    for (;;) {
        hips = skeleton.parents[static_cast<std::size_t>(hips)];
        if (hips < 0)
            break;
        const bool all = std::all_of(rig.limbs.begin(), rig.limbs.end(),
                                     [&](const IkLimb& l) { return analysis.isAncestor(hips, l.root); });
        if (all)
            break;
    }
    rig.hips = hips;

    glm::vec3 toes(0.0f), knees(0.0f);
    for (const IkLimb& l : rig.limbs) {
        if (l.kind != IkLimb::Kind::Leg)
            continue;
        const int foot = l.control >= 0 ? l.control : l.end;
        const glm::vec3 footPos = skeleton.bindModel[static_cast<std::size_t>(foot)].translation();
        for (std::size_t j = 0; j < skeleton.jointCount(); ++j) {
            if (skeleton.parents[j] == foot)
                toes += skeleton.bindModel[j].translation() - footPos;
        }
        const glm::vec3 a = skeleton.bindModel[static_cast<std::size_t>(l.root)].translation();
        const glm::vec3 b = skeleton.bindModel[static_cast<std::size_t>(l.mid)].translation();
        const glm::vec3 c = skeleton.bindModel[static_cast<std::size_t>(l.end)].translation();
        knees += b - (a + c) * 0.5f;
    }
    for (glm::vec3 dir : {toes, knees}) {
        dir.y = 0.0f;
        if (glm::length(dir) > 1e-4f) {
            rig.forward = glm::normalize(dir);
            break;
        }
    }
    return rig;
}

glm::quat modelRotation(const Affine& m)
{
    return decompose(m).r;
}

void solveTwoBoneIk(const SkeletonData& skeleton, Pose& local, std::vector<Affine>& model, int root, int mid, int end,
                    const glm::vec3& target, const glm::vec3& pole, const glm::quat* endRotation)
{
    const auto r = static_cast<std::size_t>(root), m = static_cast<std::size_t>(mid), e = static_cast<std::size_t>(end);
    glm::vec3 a = model[r].translation(), b = model[m].translation(), c = model[e].translation();
    const float upper = glm::length(b - a), lower = glm::length(c - b);
    if (upper < 1e-6f || lower < 1e-6f)
        return;
    const float reach = std::clamp(glm::length(target - a), std::abs(upper - lower) + 1e-4f, upper + lower - 1e-4f);

    // 1. Bend the middle joint until root-to-end spans the reach. The bend keeps the limb's
    // current plane; a straight limb bends towards the pole.
    // Rotating c - b about cross(a - b, c - b) by a positive angle opens the joint
    glm::vec3 axis = glm::cross(a - b, c - b);
    if (glm::length(axis) < 1e-6f * upper * lower)
        axis = glm::cross(c - a, pole - a);
    if (glm::length(axis) > 1e-12f) {
        axis = glm::normalize(axis);
        const float current = angleBetween(a - b, c - b);
        const float wanted = std::acos(std::clamp((upper * upper + lower * lower - reach * reach) / (2.0f * upper * lower), -1.0f, 1.0f));
        const glm::quat bend = glm::angleAxis(wanted - current, axis);
        setLocalRotation(skeleton, local, model, mid, aboutPoint(b, bend) * model[m].toMat4());
        localToModel(skeleton, local, model);
    }

    // 2. Swing the whole limb about the root so the end points at the target, then twist it
    // about that line so the middle joint faces the pole
    c = model[e].translation();
    b = model[m].translation();
    glm::quat swing = glm::identity<glm::quat>();
    if (glm::length(c - a) > 1e-6f && glm::length(target - a) > 1e-6f)
        swing = glm::rotation(glm::normalize(c - a), glm::normalize(target - a));
    const glm::vec3 k = glm::length(target - a) > 1e-6f ? glm::normalize(target - a) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 knee = swing * (b - a);
    glm::vec3 toPole = pole - a;
    knee -= k * glm::dot(knee, k);
    toPole -= k * glm::dot(toPole, k);
    glm::quat twist = glm::identity<glm::quat>();
    if (glm::length(knee) > 1e-6f && glm::length(toPole) > 1e-6f) {
        knee = glm::normalize(knee);
        toPole = glm::normalize(toPole);
        twist = glm::angleAxis(std::atan2(glm::dot(glm::cross(knee, toPole), k), glm::dot(knee, toPole)), k);
    }
    setLocalRotation(skeleton, local, model, root, aboutPoint(a, twist * swing) * model[r].toMat4());
    localToModel(skeleton, local, model);

    // 3. The end keeps the orientation it was given (a planted foot stays flat)
    if (endRotation) {
        const int parent = skeleton.parents[e];
        const glm::quat parentRotation = parent >= 0 ? modelRotation(model[static_cast<std::size_t>(parent)]) : glm::identity<glm::quat>();
        local.r[e] = glm::normalize(glm::inverse(parentRotation) * *endRotation);
        localToModel(skeleton, local, model);
    }
}
