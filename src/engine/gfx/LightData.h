#pragma once

#include <glm/glm.hpp>

// Point/spot light SSBO entry. Matches the std430 struct in mesh.frag.
// A point light is a spot whose cone factor is always 1: spotScale 0, spotOffset 1.
struct LightData
{
    glm::vec4 positionInvRangeSq;   // xyz world position, w = 1 / range^2
    glm::vec4 colorSpotScale;       // rgb = color * intensity
    glm::vec4 directionSpotOffset;  // xyz = direction the light points
};
static_assert(sizeof(LightData) == 48);
