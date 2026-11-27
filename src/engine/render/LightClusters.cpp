#include "LightClusters.h"

#include "engine/scene/Camera.h"

#include <algorithm>
#include <cmath>

namespace
{
int tile(float ndc, int count)
{
    return std::clamp(static_cast<int>(std::floor((ndc * 0.5f + 0.5f) * static_cast<float>(count))), 0, count - 1);
}
}

void LightClusters::build(const std::vector<LightData>& lights, const glm::mat4& view, const glm::mat4& proj)
{
    float zNear, zFar;
    clipPlanes(proj, zNear, zFar);
    const bool ortho = isOrthographic(proj);
    const float scale = ortho ? Z / (zFar - zNear) : Z / std::log(zFar / zNear);
    const float bias = -(ortho ? zNear : std::log(zNear)) * scale;
    m_sliceParams = glm::vec3(scale, bias, ortho ? 0.0f : 1.0f);
    const auto slice = [&](float depth) {
        const float s = (ortho ? depth : std::log(depth)) * scale + bias;
        return std::clamp(static_cast<int>(std::floor(s)), 0, Z - 1);
    };

    struct Bounds
    {
        int x0 = 1, x1 = 0, y0 = 1, y1 = 0, z0 = 1, z1 = 0;  // empty
    };
    std::vector<Bounds> bounds(lights.size());
    m_ranges.assign(Count, {0, 0});

    for (std::size_t i = 0; i < lights.size(); ++i) {
        const glm::vec4& posRange = lights[i].positionInvRangeSq;
        const float r = 1.0f / std::sqrt(posRange.w);
        const glm::vec3 c = glm::vec3(view * glm::vec4(glm::vec3(posRange), 1.0f));
        const float depth = -c.z;
        if (depth + r < zNear || depth - r > zFar)
            continue;

        Bounds b;
        b.z0 = slice(std::max(depth - r, zNear));
        b.z1 = slice(std::min(depth + r, zFar));
        b.x0 = 0, b.x1 = X - 1, b.y0 = 0, b.y1 = Y - 1;

        // Project the sphere's view-space box; a sphere crossing the near plane covers the whole screen
        if (ortho || depth - r > zNear) {
            glm::vec2 lo(1e30f), hi(-1e30f);
            for (int k = 0; k < 8; ++k) {
                const glm::vec3 corner = c + r * glm::vec3(k & 1 ? 1.0f : -1.0f, k & 2 ? 1.0f : -1.0f,
                                                           k & 4 ? 1.0f : -1.0f);
                const glm::vec4 clip = proj * glm::vec4(corner, 1.0f);
                const glm::vec2 ndc = glm::vec2(clip) / clip.w;
                lo = glm::min(lo, ndc);
                hi = glm::max(hi, ndc);
            }
            if (hi.x < -1.0f || lo.x > 1.0f || hi.y < -1.0f || lo.y > 1.0f)
                continue;
            b.x0 = tile(lo.x, X), b.x1 = tile(hi.x, X);
            b.y0 = tile(lo.y, Y), b.y1 = tile(hi.y, Y);
        }
        bounds[i] = b;

        for (int z = b.z0; z <= b.z1; ++z)
            for (int y = b.y0; y <= b.y1; ++y)
                for (int x = b.x0; x <= b.x1; ++x)
                    ++m_ranges[x + X * (y + Y * z)].count;
    }

    std::uint32_t total = 0;
    for (Range& range : m_ranges) {
        range.offset = total;
        total += range.count;
        range.count = 0;
    }
    m_indices.resize(total);

    for (std::size_t i = 0; i < lights.size(); ++i) {
        const Bounds& b = bounds[i];
        for (int z = b.z0; z <= b.z1; ++z)
            for (int y = b.y0; y <= b.y1; ++y)
                for (int x = b.x0; x <= b.x1; ++x) {
                    Range& range = m_ranges[x + X * (y + Y * z)];
                    m_indices[range.offset + range.count++] = static_cast<std::uint32_t>(i);
                }
    }
}
