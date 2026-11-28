#include "UiText.h"

#include "engine/ui/Font.h"
#include "engine/ui/UiComponents.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

static std::vector<std::uint32_t> decodeUtf8(std::string_view s)
{
    std::vector<std::uint32_t> out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xe ? 2 : (c >> 3) == 0x1e ? 3 : -1;
        if (extra < 0 || i + static_cast<std::size_t>(extra) >= s.size() + (extra == 0)) {
            out.push_back('?');
            ++i;
            continue;
        }
        std::uint32_t cp = extra == 0 ? c : c & (0x3f >> extra);
        bool valid = true;
        for (int k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            valid = valid && (next & 0xc0) == 0x80;
            cp = cp << 6 | (next & 0x3f);
        }
        out.push_back(valid ? cp : '?');
        i += valid ? static_cast<std::size_t>(extra) + 1 : 1;
    }
    return out;
}

bool layoutText(UITextLayout& out, const UIText& text, Font& font, glm::vec2 size)
{
    if (out.fontPtr == &font && out.text == text.text && out.font == text.font && out.fontSize == text.fontSize &&
        out.lineSpacing == text.lineSpacing && out.size == size && out.alignH == text.alignH &&
        out.alignV == text.alignV && out.wrap == text.wrap && out.overflow == text.overflow)
        return false;
    out.text = text.text;
    out.font = text.font;
    out.fontPtr = &font;
    out.fontSize = text.fontSize;
    out.lineSpacing = text.lineSpacing;
    out.size = size;
    out.alignH = text.alignH;
    out.alignV = text.alignV;
    out.wrap = text.wrap;
    out.overflow = text.overflow;
    out.quads.clear();

    const std::vector<std::uint32_t> cps = decodeUtf8(text.text);
    const float scale = text.fontSize / Font::BaseSize;
    const auto measure = [&](std::size_t begin, std::size_t end) {
        float w = 0.0f;
        for (std::size_t i = begin; i < end; ++i) {
            if (i > begin)
                w += font.kerning(cps[i - 1], cps[i]) * scale;
            w += font.glyph(cps[i]).advance * scale;
        }
        return w;
    };

    struct Line
    {
        std::size_t begin, end;
    };
    std::vector<Line> lines;
    const bool wrap = text.wrap && size.x > 0.0f;
    std::size_t start = 0, breakAt = SIZE_MAX;
    for (std::size_t i = 0; i <= cps.size();) {
        if (i == cps.size() || cps[i] == '\n') {
            lines.push_back({start, i});
            start = ++i;
            breakAt = SIZE_MAX;
            continue;
        }
        if (wrap && cps[i] != ' ' && i > start && measure(start, i + 1) > size.x) {
            const std::size_t cut = breakAt != SIZE_MAX && breakAt > start ? breakAt : i;
            lines.push_back({start, cut});
            start = cut;
            while (start < cps.size() && cps[start] == ' ')
                ++start;
            breakAt = SIZE_MAX;
            i = std::max(i, start);
            continue;
        }
        if (cps[i] == ' ')
            breakAt = i + 1;
        ++i;
    }

    const float lineHeight = (font.ascent() - font.descent() + font.lineGap()) * scale * text.lineSpacing;
    const float alignV = text.alignV == TextAlignV::Top ? 0.0f : text.alignV == TextAlignV::Middle ? 0.5f : 1.0f;
    const float alignH = text.alignH == TextAlignH::Left ? 0.0f : text.alignH == TextAlignH::Center ? 0.5f : 1.0f;
    const bool truncate = text.overflow == TextOverflow::Truncate;
    const float top = (size.y - static_cast<float>(lines.size()) * lineHeight) * alignV;

    for (std::size_t k = 0; k < lines.size(); ++k) {
        if (truncate && top + static_cast<float>(k + 1) * lineHeight > size.y + 0.5f)
            break;
        std::size_t end = lines[k].end;
        while (end > lines[k].begin && cps[end - 1] == ' ')
            --end;
        const float baseline = top + static_cast<float>(k) * lineHeight + font.ascent() * scale;
        float pen = (size.x - measure(lines[k].begin, end)) * alignH;
        for (std::size_t i = lines[k].begin; i < end; ++i) {
            if (i > lines[k].begin)
                pen += font.kerning(cps[i - 1], cps[i]) * scale;
            const Font::Glyph& g = font.glyph(cps[i]);
            if (g.size.x > 0.0f) {
                const glm::vec2 min(pen + g.offset.x * scale, baseline + g.offset.y * scale);
                const glm::vec2 max = min + g.size * scale;
                if (truncate && !text.wrap && max.x > size.x + 0.5f)
                    break;
                out.quads.push_back({min, max, glm::vec2(g.atlas), glm::vec2(g.atlas) + g.size});
            }
            pen += g.advance * scale;
        }
    }
    return true;
}
