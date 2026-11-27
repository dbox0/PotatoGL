#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <cstdint>

using MaterialId = std::uint32_t;
inline constexpr MaterialId InvalidMaterial = UINT32_MAX;

// Renderer-side material, linear values
struct Material
{
    glm::vec4 baseColor{1.0f};
    glm::vec3 emission{0.0f};  // radiance added on top of the lighting
    glm::vec2 tiling{1.0f};
    glm::vec2 offset{0.0f};
    float metallic = 0.0f;
    float smoothness = 0.5f;
    GLuint textureLayer = 0;  // 0 = white
};

// SSBO entry, indexed by DrawData::material. Matches the std430 struct in mesh.frag.
struct GpuMaterial
{
    glm::vec4 baseColor;
    glm::vec4 emission;     // w unused
    glm::vec4 uvTransform;  // tiling xy, offset zw
    float metallic;
    float smoothness;
    GLuint textureLayer;
    GLuint pad;

    static GpuMaterial from(const Material& m)
    {
        return {m.baseColor, glm::vec4(m.emission, 0.0f), glm::vec4(m.tiling, m.offset), m.metallic, m.smoothness,
                m.textureLayer, 0};
    }
};
static_assert(sizeof(GpuMaterial) == 64);
