#include "Material.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>

using json = nlohmann::ordered_json;

static constexpr int MaterialVersion = 1;

// Shortest decimal that round-trips the float
static double jsonFloat(float f)
{
    if (!std::isfinite(f))
        return 0.0;
    char buffer[32];
    const auto end = std::to_chars(buffer, buffer + sizeof(buffer), f).ptr;
    double d = 0.0;
    std::from_chars(buffer, end, d);
    return d;
}

template<int N>
static json vecJson(const glm::vec<N, float>& v)
{
    json out = json::array();
    for (int i = 0; i < N; ++i)
        out.push_back(jsonFloat(v[i]));
    return out;
}

template<int N>
static void readVec(const json& root, const char* key, glm::vec<N, float>& out)
{
    const auto it = root.find(key);
    if (it == root.end() || !it->is_array() || it->size() != N)
        return;
    for (int i = 0; i < N; ++i) {
        if ((*it)[i].is_number())
            out[i] = (*it)[i].get<float>();
    }
}

static void readFloat(const json& root, const char* key, float& out)
{
    const auto it = root.find(key);
    if (it != root.end() && it->is_number())
        out = it->get<float>();
}

std::string saveMaterial(const MaterialAsset& m)
{
    json root = json::object();
    root["version"] = MaterialVersion;
    root["shader"] = "Lit";
    root["baseColor"] = vecJson(m.baseColor);
    root["baseMap"] = m.baseMap;
    root["tiling"] = vecJson(m.tiling);
    root["offset"] = vecJson(m.offset);
    root["metallic"] = jsonFloat(m.metallic);
    root["smoothness"] = jsonFloat(m.smoothness);
    root["emission"] = vecJson(m.emission);
    root["emissionIntensity"] = jsonFloat(m.emissionIntensity);
    return root.dump(2);
}

std::optional<MaterialAsset> loadMaterial(std::string_view text, std::string* error)
{
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        if (error)
            *error = "not a JSON object";
        return std::nullopt;
    }
    if (const auto version = root.find("version");
        version != root.end() && version->is_number_integer() && version->get<int>() > MaterialVersion) {
        if (error)
            *error = "material was saved by a newer version";
        return std::nullopt;
    }

    MaterialAsset m;
    readVec(root, "baseColor", m.baseColor);
    if (const auto it = root.find("baseMap"); it != root.end() && it->is_string())
        m.baseMap = it->get<std::string>();
    readVec(root, "tiling", m.tiling);
    readVec(root, "offset", m.offset);
    readFloat(root, "metallic", m.metallic);
    readFloat(root, "smoothness", m.smoothness);
    readVec(root, "emission", m.emission);
    readFloat(root, "emissionIntensity", m.emissionIntensity);
    return m;
}

bool saveMaterialFile(const std::string& path, const MaterialAsset& material, std::string* error)
{
    std::ofstream file(path);
    if (file)
        file << saveMaterial(material) << '\n';
    if (!file) {
        if (error)
            *error = "can't write " + path;
        return false;
    }
    return true;
}

std::optional<MaterialAsset> loadMaterialFile(const std::string& path, std::string* error)
{
    std::ifstream file(path);
    if (!file) {
        if (error)
            *error = "can't open " + path;
        return std::nullopt;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return loadMaterial(ss.str(), error);
}
