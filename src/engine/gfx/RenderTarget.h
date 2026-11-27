#pragma once

#include <glad/gl.h>

// Offscreen linear HDR scene color + sRGB display color, sharing one depth buffer. GL objects are created on the first resize, so it can be
// constructed before the context exists; call release() before the context is destroyed.
class RenderTarget
{
private:
    GLuint m_fbo = 0;     // display color + depth
    GLuint m_hdrFbo = 0;  // HDR color + depth
    GLuint m_hdr = 0;     // GL_R11F_G11F_B10F, linear scene radiance
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
    void bindHdr() const;

    // For UI that draws to a non-sRGB framebuffer (ImGui)
    GLuint displayTexture() const { return m_colorView; }
    GLuint hdrTexture() const     { return m_hdr; }
    GLuint depthBuffer() const    { return m_depth; }
    int width() const             { return m_width; }
    int height() const            { return m_height; }
    bool valid() const            { return m_fbo != 0; }
};
