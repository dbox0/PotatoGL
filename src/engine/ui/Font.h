#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct stbtt_fontinfo;

// Signed distance field glyphs, baked at BaseSize into one R8 atlas that grows by doubling its
// height. ASCII and Latin-1 are baked up front, anything else on first use. Metrics are in
// BaseSize pixels; scale them by fontSize / BaseSize.
class Font
{
public:
    static constexpr float BaseSize = 48.0f;
    static constexpr int Padding = 6;

    struct Glyph
    {
        glm::vec2 offset{0.0f};  // from the pen on the baseline to the bitmap's top-left, y down
        glm::vec2 size{0.0f};    // bitmap size, padding included; 0 for blanks
        glm::ivec2 atlas{0};     // bitmap's top-left in the atlas
        float advance = 0.0f;
    };

private:
    std::vector<unsigned char> m_data;
    std::unique_ptr<stbtt_fontinfo> m_info;
    float m_scale = 1.0f;
    float m_ascent = 0.0f, m_descent = 0.0f, m_lineGap = 0.0f;
    std::unordered_map<std::uint32_t, Glyph> m_glyphs;
    std::unordered_map<std::uint64_t, float> m_kerning;
    std::vector<unsigned char> m_pixels;
    int m_width = 1024, m_height = 1024;
    int m_shelfX = 0, m_shelfY = 0, m_shelfHeight = 0;
    GLuint m_texture = 0;
    bool m_dirty = true;

    Font();
    const Glyph& bake(std::uint32_t codepoint);
    bool place(int w, int h, glm::ivec2& at);

public:
    // nullptr if the file can't be read or isn't a font
    static std::unique_ptr<Font> load(const std::string& file);
    ~Font();

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    const Glyph& glyph(std::uint32_t codepoint);
    float kerning(std::uint32_t left, std::uint32_t right);
    float ascent() const  { return m_ascent; }
    float descent() const { return m_descent; }  // negative
    float lineGap() const { return m_lineGap; }

    // Uploads glyphs baked since the last call
    GLuint texture();
    glm::vec2 atlasSize() const { return {static_cast<float>(m_width), static_cast<float>(m_height)}; }
};
