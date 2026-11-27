#include "OutlineMask.h"

#include "RenderTarget.h"

OutlineMask::~OutlineMask()
{
    release();
}

void OutlineMask::release()
{
    if (m_maskFbo == 0)
        return;
    const GLuint fbos[] = {m_maskFbo, m_dilatedFbo};
    glDeleteFramebuffers(2, fbos);
    const GLuint textures[] = {m_mask, m_dilated};
    glDeleteTextures(2, textures);
    m_maskFbo = m_dilatedFbo = m_mask = m_dilated = m_depth = 0;
    m_width = m_height = 0;
}

static GLuint createMaskTexture(int width, int height)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

void OutlineMask::resize(const RenderTarget& target)
{
    if (!target.valid())
        return;
    if (target.width() == m_width && target.height() == m_height && target.depthBuffer() == m_depth)
        return;
    release();
    m_width = target.width();
    m_height = target.height();
    m_depth = target.depthBuffer();
    m_mask = createMaskTexture(m_width, m_height);
    m_dilated = createMaskTexture(m_width, m_height);

    glGenFramebuffers(1, &m_maskFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_maskFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_mask, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);

    glGenFramebuffers(1, &m_dilatedFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_dilatedFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_dilated, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OutlineMask::bindMask() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_maskFbo);
}

void OutlineMask::bindDilated() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_dilatedFbo);
}
