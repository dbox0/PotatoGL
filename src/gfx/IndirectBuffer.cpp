#include "IndirectBuffer.h"

#include <algorithm>

IndirectBuffer::IndirectBuffer(GLsizeiptr capacity)
    : m_capacity(std::max<GLsizeiptr>(capacity, 1))
{
    glGenBuffers(1, &m_buffer);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_buffer);
    glBufferData(GL_DRAW_INDIRECT_BUFFER, m_capacity * sizeof(DrawElementsIndirectCommand), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

IndirectBuffer::~IndirectBuffer()
{
    glDeleteBuffers(1, &m_buffer);
}

void IndirectBuffer::clear()
{
    m_commands.clear();
}

void IndirectBuffer::add(const MeshRange& mesh)
{
    DrawElementsIndirectCommand cmd;
    cmd.count = mesh.indexCount;
    cmd.instanceCount = 1;
    cmd.firstIndex = mesh.firstIndex;
    cmd.baseVertex = mesh.baseVertex;
    cmd.baseInstance = static_cast<GLuint>(m_commands.size());
    m_commands.push_back(cmd);
}

void IndirectBuffer::upload()
{
    const auto count = static_cast<GLsizeiptr>(m_commands.size());
    while (m_capacity < count)
        m_capacity *= 2;

    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_buffer);
    // Orphan so the driver doesn't stall on last frame's draw still reading the buffer
    glBufferData(GL_DRAW_INDIRECT_BUFFER, m_capacity * sizeof(DrawElementsIndirectCommand), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, count * sizeof(DrawElementsIndirectCommand), m_commands.data());
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    m_uploadedCount = static_cast<GLsizei>(count);
}

void IndirectBuffer::draw() const
{
    if (m_uploadedCount == 0)
        return;
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_buffer);
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, m_uploadedCount, 0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}
