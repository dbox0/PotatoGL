#include "PickBuffer.h"

PickBuffer::~PickBuffer()
{
    release();
}

void PickBuffer::release()
{
    if (m_fbo == 0)
        return;
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteTextures(1, &m_ids);
    glDeleteRenderbuffers(1, &m_depth);
    m_fbo = m_ids = m_depth = 0;
    m_width = m_height = 0;
}

void PickBuffer::resize(int width, int height)
{
    if (width <= 0 || height <= 0 || (width == m_width && height == m_height))
        return;
    release();
    m_width = width;
    m_height = height;

    glGenTextures(1, &m_ids);
    glBindTexture(GL_TEXTURE_2D, m_ids);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RG32UI, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenRenderbuffers(1, &m_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ids, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PickBuffer::bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
}

void PickBuffer::clear() const
{
    const GLuint ids[4] = {NoId, 0, 0, 0};
    glClearBufferuiv(GL_COLOR, 0, ids);
    glClear(GL_DEPTH_BUFFER_BIT);
}

std::uint32_t PickBuffer::read(int x, int y) const
{
    if (!valid() || x < 0 || y < 0 || x >= m_width || y >= m_height)
        return NoId;
    GLuint id[2] = {NoId, 0};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(x, y, 1, 1, GL_RG_INTEGER, GL_UNSIGNED_INT, id);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return id[0];
}
