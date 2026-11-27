#include "GeometryBuffer.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
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

static void vertexAttrib(GLuint location, GLint size, GLenum type, GLboolean normalized, std::size_t offset)
{
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location, size, type, normalized, sizeof(GpuVertex), reinterpret_cast<void*>(offset));
}

static std::uint32_t packSnorm10(float v)
{
    const auto i = static_cast<std::int32_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 511.0f));
    return static_cast<std::uint32_t>(i) & 0x3FFu;
}

GpuVertex packVertex(const Vertex& v)
{
    GpuVertex g;
    g.position = v.position;
    g.normal = packSnorm10(v.normal.x) | packSnorm10(v.normal.y) << 10 | packSnorm10(v.normal.z) << 20;
    g.uv[0] = glm::packHalf1x16(v.uv.x);
    g.uv[1] = glm::packHalf1x16(v.uv.y);
    return g;
}

GeometryBuffer::GeometryBuffer(GLsizeiptr vertexCapacity, GLsizeiptr indexCapacity, GLsizeiptr drawCapacity)
    : m_vertexCapacity(std::max<GLsizeiptr>(vertexCapacity, 1)),
      m_indexCapacity(std::max<GLsizeiptr>(indexCapacity, 1)),
      m_drawCapacity(std::max<GLsizeiptr>(drawCapacity, 1))
{
    glGenVertexArrays(1, &m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_COPY_WRITE_BUFFER, m_vbo);
    glBufferData(GL_COPY_WRITE_BUFFER, m_vertexCapacity * sizeof(GpuVertex), nullptr, GL_STATIC_DRAW);

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
    vertexAttrib(0, 3, GL_FLOAT, GL_FALSE, offsetof(GpuVertex, position));
    vertexAttrib(1, 4, GL_INT_2_10_10_10_REV, GL_TRUE, offsetof(GpuVertex, normal));
    vertexAttrib(2, 2, GL_HALF_FLOAT, GL_FALSE, offsetof(GpuVertex, uv));
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
        m_vbo = grownCopy(m_vbo, m_vertexCount * sizeof(GpuVertex), capacity * sizeof(GpuVertex));
        m_vertexCapacity = capacity;
        attachVertexBuffer();
    }
    if (m_indexCount + indexCount > m_indexCapacity) {
        const GLsizeiptr capacity = grownCapacity(m_indexCapacity, m_indexCount + indexCount);
        m_ebo = grownCopy(m_ebo, m_indexCount * sizeof(std::uint32_t), capacity * sizeof(std::uint32_t));
        m_indexCapacity = capacity;
        attachIndexBuffer();
    }

    std::vector<GpuVertex> packed;
    packed.reserve(vertices.size());
    for (const Vertex& v : vertices)
        packed.push_back(packVertex(v));

    // COPY_WRITE avoids touching the ELEMENT_ARRAY binding of whatever VAO is bound
    glBindBuffer(GL_COPY_WRITE_BUFFER, m_vbo);
    glBufferSubData(GL_COPY_WRITE_BUFFER, m_vertexCount * sizeof(GpuVertex), packed.size() * sizeof(GpuVertex),
                    packed.data());
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
