#pragma once

#include <glm/glm.hpp>

#include <cstdint>

// Loader-side vertex; GeometryBuffer packs it into GpuVertex on upload
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct GpuVertex
{
    glm::vec3 position;   // location 0
    std::uint32_t normal; // location 1, GL_INT_2_10_10_10_REV, normalized
    std::uint16_t uv[2];  // location 2, GL_HALF_FLOAT
};
static_assert(sizeof(GpuVertex) == 20);

// Normal as GL_INT_2_10_10_10_REV, uv as two halves
GpuVertex packVertex(const Vertex& v);
