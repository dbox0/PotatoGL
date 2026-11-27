#include "AssetCache.h"

#include "engine/assets/Image.h"
#include "engine/assets/Model.h"
#include "engine/assets/Primitives.h"
#include "engine/core/Paths.h"

#include <cmath>
#include <cstdio>
#include <vector>

bool pathIsUnder(const std::string& path, const std::string& folder)
{
    return path.starts_with(folder) && (path.size() == folder.size() || path[folder.size()] == '/');
}

std::string rebasePath(const std::string& path, const std::string& from, const std::string& to)
{
    return to + path.substr(from.size());
}

template<typename Map>
static void renameKeys(Map& map, const std::string& from, const std::string& to)
{
    std::vector<typename Map::node_type> moved;
    for (auto it = map.begin(); it != map.end();) {
        if (pathIsUnder(it->first, from))
            moved.push_back(map.extract(it++));
        else
            ++it;
    }
    for (auto& node : moved) {
        node.key() = rebasePath(node.key(), from, to);
        map.insert_or_assign(node.key(), std::move(node.mapped()));
    }
}

static float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static glm::vec3 srgbToLinear(const glm::vec3& c)
{
    return {srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b)};
}

ModelId AssetCache::model(const std::string& path)
{
    if (const auto it = m_models.find(path); it != m_models.end())
        return it->second;

    ModelId id = InvalidModel;
    if (m_renderer) {
        const bool builtin = path.starts_with(BuiltinMeshPrefix);
        const std::optional<ModelData> data =
            builtin ? makePrimitive(std::string_view(path).substr(BuiltinMeshPrefix.size()))
                    : loadModel(resourcePath(path));
        if (data)
            id = m_renderer->addModel(*data);
    }
    if (id == InvalidModel)
        std::fprintf(stderr, "Failed to load model '%s'\n", path.c_str());
    m_models.emplace(path, id);
    return id;
}

GLuint AssetCache::texture(const std::string& path)
{
    if (const auto it = m_textures.find(path); it != m_textures.end())
        return it->second;
    GLuint layer = 0;
    if (m_renderer) {
        if (const std::optional<ImageData> image = loadImage(resourcePath(path)))
            layer = m_renderer->addTexture(*image);
    }
    m_textures.emplace(path, layer);
    return layer;
}

Material AssetCache::toRenderer(const MaterialAsset& asset)
{
    Material m;
    m.baseColor = glm::vec4(srgbToLinear(glm::vec3(asset.baseColor)), asset.baseColor.a);
    m.emission = srgbToLinear(asset.emission) * asset.emissionIntensity;
    m.tiling = asset.tiling;
    m.offset = asset.offset;
    m.metallic = asset.metallic;
    m.smoothness = asset.smoothness;
    m.textureLayer = asset.baseMap.empty() ? 0 : texture(asset.baseMap);
    return m;
}

const MaterialAsset* AssetCache::materialAsset(const std::string& path)
{
    if (const auto it = m_materials.find(path); it != m_materials.end())
        return it->second.id == InvalidMaterial ? nullptr : &it->second.asset;

    MaterialEntry entry;
    std::string error;
    if (std::optional<MaterialAsset> asset = loadMaterialFile(resourcePath(path), &error); asset && m_renderer) {
        entry.asset = std::move(*asset);
        entry.id = m_renderer->addMaterial(toRenderer(entry.asset));
    } else {
        std::fprintf(stderr, "Failed to load material '%s': %s\n", path.c_str(), error.c_str());
    }
    const auto it = m_materials.emplace(path, std::move(entry)).first;
    return it->second.id == InvalidMaterial ? nullptr : &it->second.asset;
}

MaterialId AssetCache::material(const std::string& path)
{
    if (const auto it = m_materials.find(path); it != m_materials.end())
        return it->second.id;
    materialAsset(path);
    return m_materials.at(path).id;
}

void AssetCache::setMaterialAsset(const std::string& path, const MaterialAsset& asset)
{
    if (!m_renderer)
        return;
    MaterialEntry& entry = m_materials[path];
    entry.asset = asset;
    if (entry.id == InvalidMaterial)
        entry.id = m_renderer->addMaterial(toRenderer(asset));
    else
        m_renderer->setMaterial(entry.id, toRenderer(asset));
}

void AssetCache::rename(const std::string& from, const std::string& to)
{
    renameKeys(m_models, from, to);
    renameKeys(m_textures, from, to);
    renameKeys(m_materials, from, to);
    for (auto& [path, entry] : m_materials) {
        if (pathIsUnder(entry.asset.baseMap, from))
            entry.asset.baseMap = rebasePath(entry.asset.baseMap, from, to);
    }
}
