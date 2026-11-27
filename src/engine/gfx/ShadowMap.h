#pragma once

#include <glad/gl.h>

// Depth-only map sampled with hardware comparison (sampler2DShadow), so each bilinear lookup is 2x2 PCF.
// Outside the map it reads as lit. Call release() before the context is destroyed.
class ShadowMap
{
private:
    GLuint m_fbo = 0;
    GLuint m_depth = 0;  // GL_DEPTH_COMPONENT16: half the bandwidth of 24-bit, enough for an orthographic range
    int m_size = 0;

public:
    ShadowMap() = default;
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    // No-op if the size is unchanged; ignores sizes <= 0
    void resize(int size);
    void release();
    void bind() const;

    GLuint texture() const { return m_depth; }
    int size() const       { return m_size; }
    bool valid() const     { return m_fbo != 0; }
};
