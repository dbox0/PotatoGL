#pragma once

#include <glad/gl.h>

// Layout fixed by the GL spec for glMultiDrawElementsIndirect
struct DrawElementsIndirectCommand
{
    GLuint count;
    GLuint instanceCount;
    GLuint firstIndex;
    GLint  baseVertex;
    GLuint baseInstance;  // draw index, read back as the drawId attribute
};
static_assert(sizeof(DrawElementsIndirectCommand) == 20);

struct MeshRange
{
    GLuint firstIndex = 0;
    GLuint indexCount = 0;
    GLint  baseVertex = 0;
};
