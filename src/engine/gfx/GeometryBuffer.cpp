#include "GeometryBuffer.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <vector>

static GLsizeiptr grownCapacity(GLsizeiptr current, GLsizeiptr needed)
{
    GLsizeiptr capacity = std::max<GLsizeiptr>(current, 1);
    while (capacity < needed)
        capacity *= 2;
    return capacity;
}

// Returns a new buffer of newBytes holding the first usedBytes of old; deletes old
static GLuint grownCopy(GLuint old, GLsizeiptr usedBytes, GLsizeiptr newBytes)
{
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    glBufferData(GL_COPY_WRITE_BUFFER, newBytes, nullptr, GL_STATIC_DRAW);
    if (usedBytes > 0) {
        glBindBuffer(GL_COPY_READ_BUFFER, old);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, usedBytes);
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
    }
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    glDeleteBuffers(1, &old);
    return buffer;
}

static void vertexAttrib(GLuint location, GLint size, std::size_t offset)
{
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location, size, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offset));
}

GeometryBuffer::GeometryBuffer(GLsizeiptr vertexCapacity, GLsizeiptr indexCapacity, GLsizeiptr drawCapacity)
    : m_vertexCapacity(std::max<GLsizeiptr>(vertexCapacity, 1)),
      m_indexCapacity(std::max<GLsizeiptr>(indexCapacity, 1)),
      m_drawCapacity(std::max<GLsizeiptr>(drawCapacity, 1))
{
    glGenVertexArrays(1, &m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_COPY_WRITE_BUFFER, m_vbo);
    glBufferData(GL_COPY_WRITE_BUFFER, m_vertexCapacity * sizeof(Vertex), nullptr, GL_STATIC_DRAW);

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_COPY_WRITE_BUFFER, m_ebo);
    glBufferData(GL_COPY_WRITE_BUFFER, m_indexCapacity * sizeof(std::uint32_t), nullptr, GL_STATIC_DRAW);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

    attachVertexBuffer();
    attachIndexBuffer();
    createDrawIdBuffer();
}

GeometryBuffer::~GeometryBuffer()
{
    glDeleteVertexArrays(1, &m_vao);
    const GLuint buffers[] = {m_vbo, m_ebo, m_drawIdVbo};
    glDeleteBuffers(3, buffers);
}

void GeometryBuffer::attachVertexBuffer()
{
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    vertexAttrib(0, 3, offsetof(Vertex, position));
    vertexAttrib(1, 3, offsetof(Vertex, normal));
    vertexAttrib(2, 2, offsetof(Vertex, uv));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GeometryBuffer::attachIndexBuffer()
{
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBindVertexArray(0);
}

void GeometryBuffer::createDrawIdBuffer()
{
    std::vector<GLuint> drawIds(static_cast<std::size_t>(m_drawCapacity));
    std::iota(drawIds.begin(), drawIds.end(), 0u);

    glDeleteBuffers(1, &m_drawIdVbo);
    glGenBuffers(1, &m_drawIdVbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_drawIdVbo);
    glBufferData(GL_ARRAY_BUFFER, drawIds.size() * sizeof(GLuint), drawIds.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(GLuint), nullptr);
    glVertexAttribDivisor(3, 1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

MeshRange GeometryBuffer::addMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices)
{
    const auto vertexCount = static_cast<GLsizeiptr>(vertices.size());
    const auto indexCount = static_cast<GLsizeiptr>(indices.size());

    if (m_vertexCount + vertexCount > m_vertexCapacity) {
        const GLsizeiptr capacity = grownCapacity(m_vertexCapacity, m_vertexCount + vertexCount);
        m_vbo = grownCopy(m_vbo, m_vertexCount * sizeof(Vertex), capacity * sizeof(Vertex));
        m_vertexCapacity = capacity;
        attachVertexBuffer();
    }
    if (m_indexCount + indexCount > m_indexCapacity) {
        const GLsizeiptr capacity = grownCapacity(m_indexCapacity, m_indexCount + indexCount);
        m_ebo = grownCopy(m_ebo, m_indexCount * sizeof(std::uint32_t), capacity * sizeof(std::uint32_t));
        m_indexCapacity = capacity;
        attachIndexBuffer();
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

void GeometryBuffer::reserveDraws(GLsizeiptr count)
{
    if (count <= m_drawCapacity)
        return;
    m_drawCapacity = grownCapacity(m_drawCapacity, count);
    createDrawIdBuffer();
}

void GeometryBuffer::bind() const
{
    glBindVertexArray(m_vao);
}
