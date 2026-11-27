#include "SkinnedGeometry.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <numeric>

static GLsizeiptr grownCapacity(GLsizeiptr current, GLsizeiptr needed)
{
    GLsizeiptr capacity = std::max<GLsizeiptr>(current, 1);
    while (capacity < needed)
        capacity *= 2;
    return capacity;
}

static GLuint createBuffer(GLsizeiptr bytes)
{
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    glBufferData(GL_COPY_WRITE_BUFFER, bytes, nullptr, GL_STATIC_DRAW);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    return buffer;
}

// A new buffer of newBytes holding the first usedBytes of old; deletes old
static GLuint grownCopy(GLuint old, GLsizeiptr usedBytes, GLsizeiptr newBytes)
{
    const GLuint buffer = createBuffer(newBytes);
    if (usedBytes > 0) {
        glBindBuffer(GL_COPY_READ_BUFFER, old);
        glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, usedBytes);
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }
    glDeleteBuffers(1, &old);
    return buffer;
}

// Orphans the buffer and refills it, doubling its capacity until the data fits
static void streamBuffer(GLuint buffer, GLsizeiptr& capacity, const void* data, GLsizeiptr bytes)
{
    while (capacity < bytes)
        capacity *= 2;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, capacity, nullptr, GL_STREAM_DRAW);
    if (bytes > 0)
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bytes, data);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

static GLuint packU8(const glm::u8vec4& v)
{
    return GLuint(v.x) | GLuint(v.y) << 8 | GLuint(v.z) << 16 | GLuint(v.w) << 24;
}

SkinnedGeometry::SkinnedGeometry(GLsizeiptr vertices, GLsizeiptr indices, GLsizeiptr draws)
    : m_sourceCapacity(std::max<GLsizeiptr>(vertices, 1)),
      m_indexCapacity(std::max<GLsizeiptr>(indices, 1)),
      m_outputCapacity(std::max<GLsizeiptr>(vertices, 1)),
      m_drawCapacity(std::max<GLsizeiptr>(draws, 1))
{
    m_posNormal = createBuffer(m_sourceCapacity * 4 * sizeof(GLuint));
    m_skin = createBuffer(m_sourceCapacity * 2 * sizeof(GLuint));
    m_uv = createBuffer(m_sourceCapacity * sizeof(GLuint));
    m_ebo = createBuffer(m_indexCapacity * sizeof(std::uint32_t));
    m_output = createBuffer(m_outputCapacity * sizeof(GpuVertex));
    glGenBuffers(1, &m_jobs);
    glGenBuffers(1, &m_groups);
    glGenBuffers(1, &m_palette);
    streamBuffer(m_jobs, m_jobBytes, nullptr, 0);
    streamBuffer(m_groups, m_groupBytes, nullptr, 0);
    streamBuffer(m_palette, m_paletteBytes, nullptr, 0);

    glGenVertexArrays(1, &m_vao);
    attachOutput();
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBindVertexArray(0);
    createDrawIdBuffer();
}

SkinnedGeometry::~SkinnedGeometry()
{
    glDeleteVertexArrays(1, &m_vao);
    const GLuint buffers[] = {m_posNormal, m_skin, m_uv, m_ebo, m_output, m_drawIdVbo, m_jobs, m_groups, m_palette};
    glDeleteBuffers(static_cast<GLsizei>(std::size(buffers)), buffers);
}

void SkinnedGeometry::attachOutput()
{
    auto attrib = [](GLuint location, GLint size, GLenum type, GLboolean normalized, std::size_t offset) {
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(location, size, type, normalized, sizeof(GpuVertex), reinterpret_cast<void*>(offset));
    };
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_output);
    attrib(0, 3, GL_FLOAT, GL_FALSE, offsetof(GpuVertex, position));
    attrib(1, 4, GL_INT_2_10_10_10_REV, GL_TRUE, offsetof(GpuVertex, normal));
    attrib(2, 2, GL_HALF_FLOAT, GL_FALSE, offsetof(GpuVertex, uv));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void SkinnedGeometry::createDrawIdBuffer()
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

GLuint SkinnedGeometry::addSource(std::span<const Vertex> vertices, std::span<const glm::u8vec4> joints,
                                  std::span<const glm::u8vec4> weights)
{
    const auto count = static_cast<GLsizeiptr>(vertices.size());
    if (m_sourceCount + count > m_sourceCapacity) {
        const GLsizeiptr capacity = grownCapacity(m_sourceCapacity, m_sourceCount + count);
        m_posNormal = grownCopy(m_posNormal, m_sourceCount * 4 * sizeof(GLuint), capacity * 4 * sizeof(GLuint));
        m_skin = grownCopy(m_skin, m_sourceCount * 2 * sizeof(GLuint), capacity * 2 * sizeof(GLuint));
        m_uv = grownCopy(m_uv, m_sourceCount * sizeof(GLuint), capacity * sizeof(GLuint));
        m_sourceCapacity = capacity;
    }

    std::vector<GLuint> posNormal, skin, uv;
    posNormal.reserve(vertices.size() * 4);
    skin.reserve(vertices.size() * 2);
    uv.reserve(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const GpuVertex g = packVertex(vertices[i]);
        GLuint bits[3];
        std::memcpy(bits, &g.position, sizeof(bits));
        posNormal.insert(posNormal.end(), {bits[0], bits[1], bits[2], g.normal});
        skin.push_back(i < joints.size() ? packU8(joints[i]) : 0u);
        skin.push_back(i < weights.size() ? packU8(weights[i]) : 255u);
        GLuint packedUv;
        std::memcpy(&packedUv, g.uv, sizeof(packedUv));
        uv.push_back(packedUv);
    }

    auto upload = [&](GLuint buffer, const std::vector<GLuint>& data, std::size_t perVertex) {
        glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
        glBufferSubData(GL_COPY_WRITE_BUFFER, m_sourceCount * perVertex * sizeof(GLuint),
                        data.size() * sizeof(GLuint), data.data());
    };
    upload(m_posNormal, posNormal, 4);
    upload(m_skin, skin, 2);
    upload(m_uv, uv, 1);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

    const auto first = static_cast<GLuint>(m_sourceCount);
    m_sourceCount += count;
    return first;
}

GLuint SkinnedGeometry::addIndices(std::span<const std::uint32_t> indices)
{
    const auto count = static_cast<GLsizeiptr>(indices.size());
    if (m_indexCount + count > m_indexCapacity) {
        const GLsizeiptr capacity = grownCapacity(m_indexCapacity, m_indexCount + count);
        m_ebo = grownCopy(m_ebo, m_indexCount * sizeof(std::uint32_t), capacity * sizeof(std::uint32_t));
        m_indexCapacity = capacity;
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBindVertexArray(0);
    }
    // COPY_WRITE avoids touching the ELEMENT_ARRAY binding of whatever VAO is bound
    glBindBuffer(GL_COPY_WRITE_BUFFER, m_ebo);
    glBufferSubData(GL_COPY_WRITE_BUFFER, m_indexCount * sizeof(std::uint32_t), indices.size_bytes(), indices.data());
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    const auto first = static_cast<GLuint>(m_indexCount);
    m_indexCount += count;
    return first;
}

GLuint SkinnedGeometry::allocateOutput(GLuint vertexCount)
{
    const GLuint first = m_outputRanges.allocate(vertexCount);
    const auto needed = static_cast<GLsizeiptr>(m_outputRanges.end());
    if (needed > m_outputCapacity) {
        const GLsizeiptr capacity = grownCapacity(m_outputCapacity, needed);
        m_output = grownCopy(m_output, m_outputCapacity * sizeof(GpuVertex), capacity * sizeof(GpuVertex));
        m_outputCapacity = capacity;
        attachOutput();
    }
    return first;
}

void SkinnedGeometry::freeOutput(GLuint first, GLuint vertexCount)
{
    m_outputRanges.free(first, vertexCount);
}

void SkinnedGeometry::reserveDraws(GLsizeiptr count)
{
    if (count <= m_drawCapacity)
        return;
    m_drawCapacity = grownCapacity(m_drawCapacity, count);
    createDrawIdBuffer();
}

void SkinnedGeometry::skin(std::span<const glm::vec4> palette, std::span<const Job> jobs)
{
    if (jobs.empty())
        return;
    // One workgroup table entry per group: the job it works on
    m_jobTable.assign(jobs.begin(), jobs.end());
    m_groupTable.clear();
    for (std::size_t j = 0; j < m_jobTable.size(); ++j) {
        Job& job = m_jobTable[j];
        job.firstGroup = static_cast<GLuint>(m_groupTable.size());
        const GLuint groups = (job.vertexCount + GroupSize - 1) / GroupSize;
        m_groupTable.insert(m_groupTable.end(), groups, static_cast<GLuint>(j));
    }
    GLint maxGroups = 65535;
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxGroups);
    if (m_groupTable.size() > static_cast<std::size_t>(maxGroups)) {
        std::fprintf(stderr, "Skinning: %zu workgroups exceed the limit of %d, the rest is skipped\n",
                     m_groupTable.size(), maxGroups);
        m_groupTable.resize(static_cast<std::size_t>(maxGroups));
    }
    if (m_groupTable.empty())
        return;

    streamBuffer(m_palette, m_paletteBytes, palette.data(), static_cast<GLsizeiptr>(palette.size_bytes()));
    streamBuffer(m_jobs, m_jobBytes, m_jobTable.data(), static_cast<GLsizeiptr>(m_jobTable.size() * sizeof(Job)));
    streamBuffer(m_groups, m_groupBytes, m_groupTable.data(),
                 static_cast<GLsizeiptr>(m_groupTable.size() * sizeof(GLuint)));

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_posNormal);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_skin);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_uv);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_palette);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_jobs);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_groups);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_output);
    glDispatchCompute(static_cast<GLuint>(m_groupTable.size()), 1, 1);
    glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    for (GLuint binding = 0; binding <= 6; ++binding)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, 0);
}

void SkinnedGeometry::bind() const
{
    glBindVertexArray(m_vao);
}
