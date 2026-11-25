#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

// Per-instance SSBO entry, indexed by drawId. Matches the std430 struct in mesh.vert.
// The model matrix is stored as its top three rows (affine, last row is 0 0 0 1).
struct DrawData
{
    glm::vec4 rows[3];
    GLuint textureLayer;
    GLuint pad[3];

    void setModel(const glm::mat4& m)
    {
        for (int i = 0; i < 3; ++i)
            rows[i] = glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]);
    }
};
static_assert(sizeof(DrawData) == 64);
