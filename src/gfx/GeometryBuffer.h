#pragma once

#include "DrawCommand.h"
#include "Vertex.h"

#include <cstdint>
#include <span>

// Shared VAO/VBO/EBO for all meshes. Buffers double when full via glCopyBufferSubData
// (glBufferStorage is 4.4).
class GeometryBuffer
{
private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_drawIdVbo = 0;  // location 3, divisor 1; stands in for gl_DrawID (4.6)

    GLsizeiptr m_vertexCapacity = 0;
    GLsizeiptr m_indexCapacity = 0;
    GLsizeiptr m_drawCapacity = 0;
    GLsizeiptr m_vertexCount = 0;
    GLsizeiptr m_indexCount = 0;

    void attachVertexBuffer();
    void attachIndexBuffer();
    void createDrawIdBuffer();

public:
    GeometryBuffer(GLsizeiptr vertexCapacity, GLsizeiptr indexCapacity, GLsizeiptr drawCapacity);
    ~GeometryBuffer();

    GeometryBuffer(const GeometryBuffer&) = delete;
    GeometryBuffer& operator=(const GeometryBuffer&) = delete;

    MeshRange addMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices);
    void reserveDraws(GLsizeiptr count);

    void bind() const;

    GLsizeiptr vertexCount() const    { return m_vertexCount; }
    GLsizeiptr vertexCapacity() const { return m_vertexCapacity; }
    GLsizeiptr indexCount() const     { return m_indexCount; }
    GLsizeiptr indexCapacity() const  { return m_indexCapacity; }
};
