#pragma once

#include <glad/gl.h>

// Offscreen sRGB color + depth. GL objects are created on the first resize, so it can be
// constructed before the context exists; call release() before the context is destroyed.
class RenderTarget
{
private:
    GLuint m_fbo = 0;
    GLuint m_color = 0;      // GL_SRGB8_ALPHA8, written with GL_FRAMEBUFFER_SRGB
    GLuint m_colorView = 0;  // GL_RGBA8 view of m_color: samples the encoded bytes as-is
    GLuint m_depth = 0;
    int m_width = 0;
    int m_height = 0;

public:
    RenderTarget() = default;
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    // No-op if the size is unchanged; ignores sizes <= 0
    void resize(int width, int height);
    void release();
    void bind() const;

    // For UI that draws to a non-sRGB framebuffer (ImGui)
    GLuint displayTexture() const { return m_colorView; }
    GLuint depthBuffer() const    { return m_depth; }
    int width() const             { return m_width; }
    int height() const            { return m_height; }
    bool valid() const            { return m_fbo != 0; }
};
