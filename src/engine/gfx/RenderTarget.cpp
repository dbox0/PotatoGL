#include "RenderTarget.h"

RenderTarget::~RenderTarget()
{
    release();
}

void RenderTarget::release()
{
    if (m_fbo == 0)
        return;
    const GLuint fbos[] = {m_fbo, m_hdrFbo};
    glDeleteFramebuffers(2, fbos);
    const GLuint textures[] = {m_colorView, m_color, m_hdr};
    glDeleteTextures(3, textures);
    glDeleteRenderbuffers(1, &m_depth);
    m_fbo = m_hdrFbo = m_color = m_colorView = m_hdr = m_depth = 0;
    m_width = m_height = 0;
}

void RenderTarget::resize(int width, int height)
{
    if (width <= 0 || height <= 0 || (width == m_width && height == m_height))
        return;
    release();
    m_width = width;
    m_height = height;

    // Texture views need immutable storage
    glGenTextures(1, &m_color);
    glBindTexture(GL_TEXTURE_2D, m_color);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_SRGB8_ALPHA8, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenTextures(1, &m_colorView);
    glTextureView(m_colorView, GL_TEXTURE_2D, m_color, GL_RGBA8, 0, 1, 0, 1);
    glBindTexture(GL_TEXTURE_2D, m_colorView);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 4 bytes/pixel: half the bandwidth of RGBA16F, no alpha needed
    glGenTextures(1, &m_hdr);
    glBindTexture(GL_TEXTURE_2D, m_hdr);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R11F_G11F_B10F, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenRenderbuffers(1, &m_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);

    glGenFramebuffers(1, &m_hdrFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hdr, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
}

void RenderTarget::bindHdr() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFbo);
}
