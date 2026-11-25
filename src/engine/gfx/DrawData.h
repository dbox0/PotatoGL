#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

// Per-draw SSBO entry, indexed by drawId. Matches the std430 struct in mesh.vert.
struct DrawData
{
    glm::mat4 model;
    GLuint textureLayer;
    GLuint pad[3];
};
static_assert(sizeof(DrawData) == 80);
