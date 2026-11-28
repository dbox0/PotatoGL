#include "Font.h"

#include <stb_truetype.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

static constexpr unsigned char OnEdge = 128;
static constexpr int MaxAtlasHeight = 8192;

Font::Font() : m_info(std::make_unique<stbtt_fontinfo>()) {}

Font::~Font()
{
    glDeleteTextures(1, &m_texture);
}

std::unique_ptr<Font> Font::load(const std::string& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return nullptr;
    std::unique_ptr<Font> font(new Font());
    font->m_data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    const unsigned char* data = font->m_data.data();
    const int offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0 || !stbtt_InitFont(font->m_info.get(), data, offset))
        return nullptr;
    font->m_scale = stbtt_ScaleForPixelHeight(font->m_info.get(), BaseSize);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(font->m_info.get(), &ascent, &descent, &lineGap);
    font->m_ascent = static_cast<float>(ascent) * font->m_scale;
    font->m_descent = static_cast<float>(descent) * font->m_scale;
    font->m_lineGap = static_cast<float>(lineGap) * font->m_scale;
    font->m_pixels.assign(static_cast<std::size_t>(font->m_width) * font->m_height, 0);
    for (std::uint32_t c = 32; c < 127; ++c)
        font->glyph(c);
    for (std::uint32_t c = 160; c < 256; ++c)
        font->glyph(c);
    return font;
}

bool Font::place(int w, int h, glm::ivec2& at)
{
    if (w > m_width)
        return false;
    if (m_shelfX + w > m_width) {
        m_shelfY += m_shelfHeight + 1;
        m_shelfX = 0;
        m_shelfHeight = 0;
    }
    while (m_shelfY + h > m_height) {
        if (m_height * 2 > MaxAtlasHeight)
            return false;
        m_height *= 2;
        m_pixels.resize(static_cast<std::size_t>(m_width) * m_height, 0);
    }
    at = {m_shelfX, m_shelfY};
    m_shelfX += w + 1;
    m_shelfHeight = std::max(m_shelfHeight, h);
    return true;
}

const Font::Glyph& Font::bake(std::uint32_t codepoint)
{
    Glyph glyph;
    const int cp = static_cast<int>(codepoint);
    if (stbtt_FindGlyphIndex(m_info.get(), cp) == 0 && codepoint != '?' && codepoint > 32)
        return m_glyphs[codepoint] = bake('?');

    int advance = 0, bearing = 0;
    stbtt_GetCodepointHMetrics(m_info.get(), cp, &advance, &bearing);
    glyph.advance = static_cast<float>(advance) * m_scale;

    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* sdf = stbtt_GetCodepointSDF(m_info.get(), m_scale, cp, Padding, OnEdge,
                                               static_cast<float>(OnEdge) / Padding, &w, &h, &xoff, &yoff);
    glm::ivec2 at;
    if (sdf && w > 0 && h > 0 && place(w, h, at)) {
        for (int y = 0; y < h; ++y)
            std::memcpy(&m_pixels[static_cast<std::size_t>(at.y + y) * m_width + at.x], sdf + y * w,
                        static_cast<std::size_t>(w));
        glyph.offset = {static_cast<float>(xoff), static_cast<float>(yoff)};
        glyph.size = {static_cast<float>(w), static_cast<float>(h)};
        glyph.atlas = at;
        m_dirty = true;
    }
    if (sdf)
        stbtt_FreeSDF(sdf, nullptr);
    return m_glyphs[codepoint] = glyph;
}

const Font::Glyph& Font::glyph(std::uint32_t codepoint)
{
    if (const auto it = m_glyphs.find(codepoint); it != m_glyphs.end())
        return it->second;
    return bake(codepoint);
}

float Font::kerning(std::uint32_t left, std::uint32_t right)
{
    const std::uint64_t key = static_cast<std::uint64_t>(left) << 32 | right;
    if (const auto it = m_kerning.find(key); it != m_kerning.end())
        return it->second;
    const float k = static_cast<float>(stbtt_GetCodepointKernAdvance(m_info.get(), static_cast<int>(left),
                                                                     static_cast<int>(right))) * m_scale;
    return m_kerning[key] = k;
}

GLuint Font::texture()
{
    if (!m_dirty)
        return m_texture;
    if (m_texture == 0)
        glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_width, m_height, 0, GL_RED, GL_UNSIGNED_BYTE, m_pixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_dirty = false;
    return m_texture;
}
