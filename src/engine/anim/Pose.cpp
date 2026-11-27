#include "Pose.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

Affine Affine::fromMat4(const glm::mat4& m)
{
    Affine a;
    for (int i = 0; i < 3; ++i)
        a.rows[i] = glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]);
    return a;
}

Affine Affine::fromTrs(const glm::vec3& t, const glm::quat& r, const glm::vec3& s)
{
    const glm::mat3 m = glm::mat3_cast(r);
    Affine a;
    for (int i = 0; i < 3; ++i)
        a.rows[i] = glm::vec4(m[0][i] * s.x, m[1][i] * s.y, m[2][i] * s.z, t[i]);
    return a;
}

glm::mat4 Affine::toMat4() const
{
    glm::mat4 m(1.0f);
    for (int i = 0; i < 3; ++i) {
        m[0][i] = rows[i].x;
        m[1][i] = rows[i].y;
        m[2][i] = rows[i].z;
        m[3][i] = rows[i].w;
    }
    return m;
}

std::uint32_t jointNameHash(std::string_view name)
{
    std::uint32_t h = 2166136261u;
    for (const char c : name) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    return h;
}

int SkeletonData::findJoint(std::string_view name) const
{
    const std::uint32_t hash = jointNameHash(name);
    for (std::size_t i = 0; i < nameHashes.size(); ++i) {
        if (nameHashes[i] == hash && names[i] == name)
            return static_cast<int>(i);
    }
    return -1;
}

void Pose::setBind(const SkeletonData& skeleton)
{
    const std::size_t count = skeleton.jointCount();
    t.resize(count);
    r.resize(count);
    s.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        t[i] = skeleton.bindLocal[i].t;
        r[i] = skeleton.bindLocal[i].r;
        s[i] = skeleton.bindLocal[i].s;
    }
}

std::vector<std::int16_t> bindClip(const ClipData& clip, const SkeletonData& skeleton)
{
    std::vector<std::int16_t> binding(clip.tracks.size(), -1);
    std::size_t missing = 0;
    for (std::size_t k = 0; k < clip.tracks.size(); ++k) {
        const auto it = std::find(skeleton.nameHashes.begin(), skeleton.nameHashes.end(), clip.tracks[k].jointHash);
        if (it == skeleton.nameHashes.end())
            ++missing;
        else
            binding[k] = static_cast<std::int16_t>(it - skeleton.nameHashes.begin());
    }
    if (missing > 0)
        std::fprintf(stderr, "Clip '%s': %zu of %zu tracks have no joint in the skeleton, dropped\n",
                     clip.name.c_str(), missing, clip.tracks.size());
    return binding;
}

static glm::vec3 lerp3(const float* a, const float* b, float t)
{
    return glm::mix(glm::vec3(a[0], a[1], a[2]), glm::vec3(b[0], b[1], b[2]), t);
}

// Import keeps neighbouring frames in the same hemisphere, so no sign flip is needed
static glm::quat nlerp(const float* a, const float* b, float t)
{
    const glm::vec4 q = glm::mix(glm::vec4(a[0], a[1], a[2], a[3]), glm::vec4(b[0], b[1], b[2], b[3]), t);
    const float len2 = glm::dot(q, q);
    const glm::vec4 n = len2 > 1e-12f ? q / std::sqrt(len2) : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return glm::quat(n.w, n.x, n.y, n.z);
}

void sampleClip(const ClipData& clip, std::span<const std::int16_t> binding, float time, Pose& pose)
{
    if (clip.frameCount == 0)
        return;
    const float last = static_cast<float>(clip.frameCount - 1);
    const float f = std::clamp(time * clip.rate, 0.0f, last);
    const auto i = static_cast<std::uint32_t>(f);
    const float a = f - static_cast<float>(i);
    const std::uint32_t i1 = std::min(i + 1, clip.frameCount - 1);
    const float* frameA = clip.frames.data() + static_cast<std::size_t>(i) * clip.frameStride;
    const float* frameB = clip.frames.data() + static_cast<std::size_t>(i1) * clip.frameStride;

    const std::size_t count = std::min(binding.size(), clip.tracks.size());
    for (std::size_t k = 0; k < count; ++k) {
        const int j = binding[k];
        if (j < 0)
            continue;
        const ClipData::Track& track = clip.tracks[k];
        std::uint32_t offset = track.offset;
        if (track.animated & ClipData::Translation) {
            pose.t[j] = lerp3(frameA + offset, frameB + offset, a);
            offset += 3;
        } else {
            pose.t[j] = track.constant.t;
        }
        if (track.animated & ClipData::Rotation) {
            pose.r[j] = nlerp(frameA + offset, frameB + offset, a);
            offset += 4;
        } else {
            pose.r[j] = track.constant.r;
        }
        if (track.animated & ClipData::Scale)
            pose.s[j] = lerp3(frameA + offset, frameB + offset, a);
        else
            pose.s[j] = track.constant.s;
    }
}

void localToModel(const SkeletonData& skeleton, const Pose& pose, std::span<Affine> model)
{
    const std::size_t count = std::min(skeleton.jointCount(), model.size());
    for (std::size_t j = 0; j < count; ++j) {
        const Affine local = Affine::fromTrs(pose.t[j], pose.r[j], pose.s[j]);
        const int parent = skeleton.parents[j];
        model[j] = parent < 0 ? local : model[static_cast<std::size_t>(parent)] * local;
    }
}

void writePalette(const SkeletonData& skeleton, std::span<const Affine> model, std::span<glm::vec4> palette)
{
    const std::size_t count = std::min({skeleton.jointCount(), model.size(), palette.size() / 3});
    for (std::size_t j = 0; j < count; ++j) {
        const Affine m = model[j] * skeleton.inverseBind[j];
        palette[j * 3 + 0] = m.rows[0];
        palette[j * 3 + 1] = m.rows[1];
        palette[j * 3 + 2] = m.rows[2];
    }
}

float advanceClipTime(float time, float dt, float duration, bool loop)
{
    if (duration <= 0.0f)
        return 0.0f;
    time += dt;
    if (!loop)
        return std::clamp(time, 0.0f, duration);
    time = std::fmod(time, duration);
    return time < 0.0f ? time + duration : time;
}

JointTrs decompose(const Affine& m)
{
    glm::vec3 cols[3];
    for (int c = 0; c < 3; ++c)
        cols[c] = glm::vec3(m.rows[0][c], m.rows[1][c], m.rows[2][c]);
    JointTrs out;
    out.t = m.translation();
    out.s = glm::vec3(glm::length(cols[0]), glm::length(cols[1]), glm::length(cols[2]));
    if (glm::dot(glm::cross(cols[0], cols[1]), cols[2]) < 0.0f)
        out.s.x = -out.s.x;
    glm::mat3 rot;
    for (int c = 0; c < 3; ++c)
        rot[c] = std::abs(out.s[c]) > 1e-8f ? cols[c] / out.s[c] : glm::vec3(c == 0, c == 1, c == 2);
    out.r = glm::normalize(glm::quat_cast(rot));
    return out;
}
