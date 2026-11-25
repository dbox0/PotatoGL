#pragma once

#include <glad/gl.h>

// Shared GL_TEXTURE_2D_ARRAY for all textures; every layer has the same size.
// Layer 0 is a white fallback for untextured meshes. Doubles its layer count when full.
class TextureArray
{
private:
    GLuint m_texture = 0;
    GLuint m_readFbo = 0;
    GLuint m_drawFbo = 0;
    GLsizei m_width = 0;
    GLsizei m_height = 0;
    GLsizei m_levels = 0;
    GLsizei m_layerCapacity = 0;
    GLsizei m_layerCount = 0;

    GLuint createTexture(GLsizei layers) const;
    void grow();

public:
    TextureArray(GLsizei width, GLsizei height, GLsizei layerCapacity);
    ~TextureArray();

    TextureArray(const TextureArray&) = delete;
    TextureArray& operator=(const TextureArray&) = delete;

    // sRGB RGBA8 pixels, any size (scaled on the GPU if it differs). Returns the layer.
    GLuint addLayer(GLsizei width, GLsizei height, const unsigned char* rgba);

    // Call once after adding layers
    void generateMipmaps() const;

    void bind(GLuint unit) const;

    GLsizei layerCount() const    { return m_layerCount; }
    GLsizei layerCapacity() const { return m_layerCapacity; }
    GLsizei layerSize() const     { return m_width; }
};
