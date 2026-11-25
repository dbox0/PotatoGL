#include "GeometryBuffer.h"

#include <cstddef>
#include <cstdio>
#include <numeric>
#include <vector>

static void vertexAttrib(GLuint location, GLint size, std::size_t offset)
{
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location, size, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offset));
}

GeometryBuffer::GeometryBuffer(GLsizeiptr maxVertices, GLsizeiptr maxIndices, GLuint maxDraws)
    : m_maxVertices(maxVertices), m_maxIndices(maxIndices)
{
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, maxVertices * sizeof(Vertex), nullptr, GL_STATIC_DRAW);
    vertexAttrib(0, 3, offsetof(Vertex, position));
    vertexAttrib(1, 3, offsetof(Vertex, normal));
    vertexAttrib(2, 2, offsetof(Vertex, uv));

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, maxIndices * sizeof(std::uint32_t), nullptr, GL_STATIC_DRAW);

    std::vector<GLuint> drawIds(maxDraws);
    std::iota(drawIds.begin(), drawIds.end(), 0u);
    glGenBuffers(1, &m_drawIdVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_drawIdVbo);
    glBufferData(GL_ARRAY_BUFFER, drawIds.size() * sizeof(GLuint), drawIds.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(GLuint), nullptr);
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

GeometryBuffer::~GeometryBuffer()
{
    glDeleteVertexArrays(1, &m_vao);
    const GLuint buffers[] = {m_vbo, m_ebo, m_drawIdVbo};
    glDeleteBuffers(3, buffers);
}

MeshRange GeometryBuffer::addMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices)
{
    const auto vertexCount = static_cast<GLsizeiptr>(vertices.size());
    const auto indexCount = static_cast<GLsizeiptr>(indices.size());
    if (m_vertexCount + vertexCount > m_maxVertices || m_indexCount + indexCount > m_maxIndices) {
        std::fprintf(stderr, "GeometryBuffer full: dropping mesh (%zu vertices, %zu indices)\n",
                     vertices.size(), indices.size());
        return {};
    }

    // COPY_WRITE avoids touching the ELEMENT_ARRAY binding of whatever VAO is bound
    glBindBuffer(GL_COPY_WRITE_BUFFER, m_vbo);
    glBufferSubData(GL_COPY_WRITE_BUFFER, m_vertexCount * sizeof(Vertex), vertices.size_bytes(), vertices.data());
    glBindBuffer(GL_COPY_WRITE_BUFFER, m_ebo);
    glBufferSubData(GL_COPY_WRITE_BUFFER, m_indexCount * sizeof(std::uint32_t), indices.size_bytes(), indices.data());
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

    MeshRange range;
    range.firstIndex = static_cast<GLuint>(m_indexCount);
    range.indexCount = static_cast<GLuint>(indexCount);
    range.baseVertex = static_cast<GLint>(m_vertexCount);

    m_vertexCount += vertexCount;
    m_indexCount += indexCount;
    return range;
}

void GeometryBuffer::bind() const
{
    glBindVertexArray(m_vao);
}
