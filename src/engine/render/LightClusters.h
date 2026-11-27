#pragma once

#include "engine/gfx/LightData.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Froxel grid over the view: screen tiles x depth slices (exponential, linear when orthographic).
// Each cluster lists the lights whose range sphere may reach it, so a fragment loops over only those.
// The grid size must match ClusterGrid in mesh.frag.
class LightClusters
{
public:
    static constexpr int X = 16;
    static constexpr int Y = 9;
    static constexpr int Z = 24;
    static constexpr int Count = X * Y * Z;

    // Matches uvec2 in mesh.frag
    struct Range
    {
        std::uint32_t offset;
        std::uint32_t count;
    };

private:
    std::vector<Range> m_ranges;
    std::vector<std::uint32_t> m_indices;
    glm::vec3 m_sliceParams{0.0f};

public:
    void build(const std::vector<LightData>& lights, const glm::mat4& view, const glm::mat4& proj);

    const std::vector<Range>& ranges() const          { return m_ranges; }
    const std::vector<std::uint32_t>& indices() const { return m_indices; }
    // slice = f(depth) * x + y, where f is log when z = 1 and identity when z = 0
    const glm::vec3& sliceParams() const { return m_sliceParams; }
};
