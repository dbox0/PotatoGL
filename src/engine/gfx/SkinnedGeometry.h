#pragma once

#include "engine/gfx/DrawCommand.h"
#include "engine/gfx/RangeAllocator.h"
#include "engine/gfx/Vertex.h"

#include <glm/glm.hpp>
#include <glm/ext/vector_uint4_sized.hpp>

#include <cstdint>
#include <span>
#include <vector>

// Skinned meshes: source vertices (compute input, SSBOs, structure of arrays), their own
// index buffer, and an output VBO of plain GpuVertex with one region per skinned instance.
// skin.comp fills the regions once per frame; every render pass then draws them through
// this VAO with the same vertex shader as static meshes.
class SkinnedGeometry
{
public:
    static constexpr GLuint GroupSize = 64;  // skin.comp local_size_x

    // Matches the std430 struct in skin.comp
    struct Job
    {
        GLuint srcFirst;
        GLuint dstFirst;
        GLuint vertexCount;
        GLuint jointBase;   // first joint of the instance's palette slice
        GLuint firstGroup;  // filled in by skin()
        GLuint pad[3];
    };
    static_assert(sizeof(Job) == 32);

private:
    GLuint m_posNormal = 0;  // uvec4: position bits, packed normal
    GLuint m_skin = 0;       // uvec2: 4 x u8 joints, 4 x unorm8 weights
    GLuint m_uv = 0;         // 2 x half
    GLuint m_ebo = 0;
    GLuint m_output = 0;  // GpuVertex
    GLuint m_vao = 0;
    GLuint m_drawIdVbo = 0;
    GLuint m_jobs = 0;
    GLuint m_groups = 0;
    GLuint m_palette = 0;

    GLsizeiptr m_sourceCapacity = 0;
    GLsizeiptr m_sourceCount = 0;
    GLsizeiptr m_indexCapacity = 0;
    GLsizeiptr m_indexCount = 0;
    GLsizeiptr m_outputCapacity = 0;
    GLsizeiptr m_drawCapacity = 0;
    GLsizeiptr m_jobBytes = 64 * sizeof(Job);
    GLsizeiptr m_groupBytes = 256 * sizeof(GLuint);
    GLsizeiptr m_paletteBytes = 256 * 3 * sizeof(glm::vec4);
    RangeAllocator m_outputRanges;
    std::vector<GLuint> m_groupTable;
    std::vector<Job> m_jobTable;

    void attachOutput();
    void createDrawIdBuffer();

public:
    SkinnedGeometry(GLsizeiptr vertices, GLsizeiptr indices, GLsizeiptr draws);
    ~SkinnedGeometry();

    SkinnedGeometry(const SkinnedGeometry&) = delete;
    SkinnedGeometry& operator=(const SkinnedGeometry&) = delete;

    // One model's skinned vertices; returns the first source vertex
    GLuint addSource(std::span<const Vertex> vertices, std::span<const glm::u8vec4> joints,
                     std::span<const glm::u8vec4> weights);
    // A submesh's indices, relative to its own first vertex; returns the first index
    GLuint addIndices(std::span<const std::uint32_t> indices);

    // An instance's output region
    GLuint allocateOutput(GLuint vertexCount);
    void freeOutput(GLuint first, GLuint vertexCount);
    void reserveDraws(GLsizeiptr count);

    // Uploads the palette (three rows per joint) and the jobs, then skins them all in one
    // dispatch. skin.comp must be the current program.
    void skin(std::span<const glm::vec4> palette, std::span<const Job> jobs);

    void bind() const;

    GLsizeiptr sourceVertexCount() const { return m_sourceCount; }
    GLsizeiptr outputVertexCount() const { return m_outputRanges.end(); }
    GLsizeiptr indexCount() const        { return m_indexCount; }
};
