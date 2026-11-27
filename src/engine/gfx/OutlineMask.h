#pragma once

#include <glad/gl.h>

class RenderTarget;

// R8 silhouette of the highlighted instances plus its horizontal dilation, for the selection outline.
// Shares the RenderTarget's depth so hidden parts stay unmasked. Call release() before the context goes.
class OutlineMask
{
private:
    GLuint m_maskFbo = 0;
    GLuint m_dilatedFbo = 0;
    GLuint m_mask = 0;
    GLuint m_dilated = 0;
    GLuint m_depth = 0;  // the target's renderbuffer, not owned
    int m_width = 0;
    int m_height = 0;

public:
    OutlineMask() = default;
    ~OutlineMask();

    OutlineMask(const OutlineMask&) = delete;
    OutlineMask& operator=(const OutlineMask&) = delete;

    // Matches target's size and depth buffer; no-op if both are unchanged
    void resize(const RenderTarget& target);
    void release();
    void bindMask() const;
    void bindDilated() const;

    GLuint maskTexture() const    { return m_mask; }
    GLuint dilatedTexture() const { return m_dilated; }
    int width() const             { return m_width; }
    int height() const            { return m_height; }
    bool valid() const            { return m_maskFbo != 0; }
};
