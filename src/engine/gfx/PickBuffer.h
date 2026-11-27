#pragma once

#include <glad/gl.h>

#include <cstdint>

// GL_RG32UI ids + depth for picking and outlines: R = entity id, G = highlight.
// Created on the first resize; call release() before the context is destroyed.
class PickBuffer
{
private:
    GLuint m_fbo = 0;
    GLuint m_ids = 0;
    GLuint m_depth = 0;
    int m_width = 0;
    int m_height = 0;

public:
    static constexpr std::uint32_t NoId = UINT32_MAX;

    PickBuffer() = default;
    ~PickBuffer();

    PickBuffer(const PickBuffer&) = delete;
    PickBuffer& operator=(const PickBuffer&) = delete;

    void resize(int width, int height);
    void release();
    void bind() const;
    void clear() const;  // ids to NoId, depth to 1; must be bound

    // Synchronous readback; pixel origin is bottom-left. NoId outside or on empty space.
    std::uint32_t read(int x, int y) const;

    GLuint idTexture() const { return m_ids; }
    int width() const        { return m_width; }
    int height() const       { return m_height; }
    bool valid() const       { return m_fbo != 0; }
};
