#pragma once

#include "DrawCommand.h"

#include <vector>

class IndirectBuffer
{
private:
    GLuint m_buffer = 0;
    GLsizeiptr m_capacity = 0;
    GLsizei m_uploadedCount = 0;
    std::vector<DrawElementsIndirectCommand> m_commands;

public:
    explicit IndirectBuffer(GLsizeiptr capacity);
    ~IndirectBuffer();

    IndirectBuffer(const IndirectBuffer&) = delete;
    IndirectBuffer& operator=(const IndirectBuffer&) = delete;

    void clear();
    void add(const MeshRange& mesh);  // baseInstance = command index
    void upload();                    // grows the GPU buffer if needed

    // Draws what was last uploaded; GeometryBuffer must be bound
    void draw() const;

    GLsizei size() const { return m_uploadedCount; }
    GLsizeiptr capacity() const { return m_capacity; }
};
