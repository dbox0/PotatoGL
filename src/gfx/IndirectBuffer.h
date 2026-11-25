#pragma once

#include "DrawCommand.h"

#include <vector>

class IndirectBuffer
{
private:
    GLuint m_buffer = 0;
    GLuint m_maxDraws = 0;
    GLsizei m_uploadedCount = 0;
    std::vector<DrawElementsIndirectCommand> m_commands;

public:
    explicit IndirectBuffer(GLuint maxDraws);
    ~IndirectBuffer();

    IndirectBuffer(const IndirectBuffer&) = delete;
    IndirectBuffer& operator=(const IndirectBuffer&) = delete;

    void clear();
    void add(const MeshRange& mesh);
    void upload();

    // Draws what was last uploaded; GeometryBuffer must be bound
    void draw() const;
};
