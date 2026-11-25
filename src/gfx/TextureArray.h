#pragma once

#include <glad/gl.h>

// Shared GL_TEXTURE_2D_ARRAY for all textures; every layer has the same size.
// Layer 0 is a white fallback for untextured meshes.
class TextureArray
{
private:
    GLuint m_texture = 0;
    GLuint m_readFbo = 0;
    GLuint m_drawFbo = 0;
    GLsizei m_width = 0;
    GLsizei m_height = 0;
    GLsizei m_maxLayers = 0;
    GLsizei m_layerCount = 0;

public:
    TextureArray(GLsizei width, GLsizei height, GLsizei maxLayers);
    ~TextureArray();

    TextureArray(const TextureArray&) = delete;
    TextureArray& operator=(const TextureArray&) = delete;

    // RGBA8 pixels, any size (scaled on the GPU if it differs). Returns the layer, or 0 if full.
    GLuint addLayer(GLsizei width, GLsizei height, const unsigned char* rgba);

    // Call once after adding layers
    void generateMipmaps() const;

    void bind(GLuint unit) const;
};
