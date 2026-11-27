#include "SpriteMeta.h"

#include <nlohmann/json.hpp>

#include <fstream>

using json = nlohmann::ordered_json;

SpriteMeta loadSpriteMeta(const std::string& file)
{
    SpriteMeta meta;
    std::ifstream in(spriteMetaPath(file));
    if (!in)
        return meta;
    const json j = json::parse(in, nullptr, false);
    if (!j.is_object())
        return meta;
    if (const auto border = j.find("border"); border != j.end() && border->is_array() && border->size() == 4) {
        for (int i = 0; i < 4; ++i) {
            if ((*border)[i].is_number())
                meta.border[i] = (*border)[i].get<float>();
        }
    }
    if (const auto ppu = j.find("pixelsPerUnit"); ppu != j.end() && ppu->is_number() && ppu->get<float>() > 0.0f)
        meta.pixelsPerUnit = ppu->get<float>();
    if (const auto filter = j.find("filter"); filter != j.end() && filter->is_string())
        meta.filter = filter->get<std::string>() == "point" ? SpriteFilter::Point : SpriteFilter::Linear;
    return meta;
}

bool saveSpriteMeta(const std::string& file, const SpriteMeta& meta)
{
    json j = json::object();
    j["border"] = json::array({meta.border.x, meta.border.y, meta.border.z, meta.border.w});
    j["pixelsPerUnit"] = meta.pixelsPerUnit;
    j["filter"] = meta.filter == SpriteFilter::Point ? "point" : "linear";
    std::ofstream out(spriteMetaPath(file));
    out << j.dump(2) << '\n';
    return static_cast<bool>(out);
}
