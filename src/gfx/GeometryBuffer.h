#pragma once

#include "DrawCommand.h"
#include "Vertex.h"

#include <cstdint>
#include <span>

// Shared VAO/VBO/EBO for all meshes. Fixed capacity: glBufferStorage is 4.4.
class GeometryBuffer
{
private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_drawIdVbo = 0;  // location 3, divisor 1; stands in for gl_DrawID (4.6)

    GLsizeiptr m_maxVertices = 0;
    GLsizeiptr m_maxIndices = 0;
    GLsizeiptr m_vertexCount = 0;
    GLsizeiptr m_indexCount = 0;

public:
    GeometryBuffer(GLsizeiptr maxVertices, GLsizeiptr maxIndices, GLuint maxDraws);
    ~GeometryBuffer();

    GeometryBuffer(const GeometryBuffer&) = delete;
    GeometryBuffer& operator=(const GeometryBuffer&) = delete;

    // Returns an empty range if capacity is exceeded
    MeshRange addMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices);

    void bind() const;
};
