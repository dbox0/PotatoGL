#include "ShadowMap.h"

ShadowMap::~ShadowMap()
{
    release();
}

void ShadowMap::release()
{
    if (m_fbo == 0)
        return;
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteTextures(1, &m_depth);
    m_fbo = m_depth = 0;
    m_size = 0;
}

void ShadowMap::resize(int size)
{
    if (size <= 0 || size == m_size)
        return;
    release();
    m_size = size;

    glGenTextures(1, &m_depth);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT16, size, size);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const GLfloat border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShadowMap::bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
}
