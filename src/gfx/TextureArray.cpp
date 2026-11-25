#include "TextureArray.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

TextureArray::TextureArray(GLsizei width, GLsizei height, GLsizei maxLayers)
    : m_width(width), m_height(height), m_maxLayers(maxLayers)
{
    const auto levels = static_cast<GLsizei>(std::floor(std::log2(std::max(width, height)))) + 1;

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, levels, GL_RGBA8, width, height, maxLayers);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    glGenFramebuffers(1, &m_readFbo);
    glGenFramebuffers(1, &m_drawFbo);

    const std::vector<unsigned char> white(static_cast<std::size_t>(width) * height * 4, 255);
    addLayer(width, height, white.data());
}

TextureArray::~TextureArray()
{
    glDeleteTextures(1, &m_texture);
    const GLuint fbos[] = {m_readFbo, m_drawFbo};
    glDeleteFramebuffers(2, fbos);
}

GLuint TextureArray::addLayer(GLsizei width, GLsizei height, const unsigned char* rgba)
{
    if (m_layerCount >= m_maxLayers) {
        std::fprintf(stderr, "TextureArray full: using fallback layer\n");
        return 0;
    }
    const GLsizei layer = m_layerCount++;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (width == m_width && height == m_height) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        return static_cast<GLuint>(layer);
    }

    // Size mismatch: upload to a temp texture and let the GPU scale it into the layer
    GLuint temp;
    glGenTextures(1, &temp);
    glBindTexture(GL_TEXTURE_2D, temp);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, temp, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_drawFbo);
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_texture, 0, layer);
    glBlitFramebuffer(0, 0, width, height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDeleteTextures(1, &temp);
    return static_cast<GLuint>(layer);
}

void TextureArray::generateMipmaps() const
{
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void TextureArray::bind(GLuint unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
}
