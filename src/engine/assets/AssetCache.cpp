#include "AssetCache.h"

#include "engine/assets/Image.h"
#include "engine/assets/Model.h"
#include "engine/assets/Primitives.h"
#include "engine/core/Paths.h"
#include "engine/ui/Font.h"

#include "engine/anim/AnimAsset.h"
#include "engine/assets/AssetTypes.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

bool pathIsUnder(const std::string& path, const std::string& folder)
{
    return path.starts_with(folder) &&
           (path.size() == folder.size() || path[folder.size()] == '/' || path[folder.size()] == '#');
}

std::string clipModelPath(const std::string& ref)
{
    return ref.substr(0, ref.find('#'));
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
        std::optional<ModelData> data =
            builtin ? makePrimitive(std::string_view(path).substr(BuiltinMeshPrefix.size()))
                    : loadModel(resourcePath(path));
        if (data)
            id = m_renderer->addModel(*data);
        if (id != InvalidModel && data->skeleton && m_renderer->modelSkinned(id)) {
            Animation& animation = m_animations[path];
            animation.skeleton = std::make_shared<const SkeletonData>(std::move(*data->skeleton));
            for (ClipData& clip : data->clips)
                animation.clips.push_back(std::make_shared<const ClipData>(std::move(clip)));
        }
    }
    if (id == InvalidModel)
        std::fprintf(stderr, "Failed to load model '%s'\n", path.c_str());
    m_models.emplace(path, id);
    return id;
}

std::shared_ptr<const SkeletonData> AssetCache::skeleton(const std::string& modelPath)
{
    model(modelPath);
    const auto it = m_animations.find(modelPath);
    return it == m_animations.end() ? nullptr : it->second.skeleton;
}

std::shared_ptr<const ClipData> AssetCache::clip(const std::string& ref)
{
    const std::size_t hash = ref.find('#');
    if (hash == std::string::npos) {
        if (assetKindOf(ref) != AssetKind::Animation)
            return nullptr;
        if (const auto it = m_animFiles.find(ref); it != m_animFiles.end())
            return it->second;
        std::shared_ptr<const ClipData> loaded;
        std::ifstream file(resourcePath(ref), std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        ClipData baked;
        std::string error;
        if (file && loadAnimText(text.str(), nullptr, &baked, std::filesystem::path(ref).stem().string(), &error))
            loaded = std::make_shared<const ClipData>(std::move(baked));
        else
            std::fprintf(stderr, "Failed to load clip '%s': %s\n", ref.c_str(), error.empty() ? "can't read it" : error.c_str());
        m_animFiles.emplace(ref, loaded);
        return loaded;
    }
    const std::string path = ref.substr(0, hash);
    model(path);
    const auto it = m_animations.find(path);
    if (it == m_animations.end())
        return nullptr;
    const std::string_view name = std::string_view(ref).substr(hash + 1);
    for (const auto& clip : it->second.clips) {
        if (clip->name == name)
            return clip;
    }
    return nullptr;
}

void AssetCache::setClip(const std::string& path, std::shared_ptr<const ClipData> clip)
{
    if (clip)
        m_animFiles[path] = std::move(clip);
    else
        m_animFiles.erase(path);
    ++m_clipVersion;
}

std::vector<std::string> AssetCache::clipRefs(const std::string& modelPath)
{
    std::vector<std::string> refs;
    model(modelPath);
    if (const auto it = m_animations.find(modelPath); it != m_animations.end()) {
        for (const auto& clip : it->second.clips)
            refs.push_back(modelPath + "#" + clip->name);
    }
    return refs;
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

static std::filesystem::file_time_type fileTime(const std::string& file)
{
    std::error_code ec;
    return std::filesystem::last_write_time(file, ec);
}

static void applyFilter(TextureId id, SpriteFilter filter)
{
    glBindTexture(GL_TEXTURE_2D, id);
    const bool point = filter == SpriteFilter::Point;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, point ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, point ? GL_NEAREST : GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool AssetCache::loadTexture2D(const std::string& path, Texture2D& entry)
{
    const std::string file = resourcePath(path);
    entry.imageTime = fileTime(file);
    entry.metaTime = fileTime(spriteMetaPath(file));
    const std::optional<ImageData> image = loadImage(file);
    if (!image)
        return false;
    if (entry.id == InvalidTexture)
        glGenTextures(1, &entry.id);
    glBindTexture(GL_TEXTURE_2D, entry.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image->width, image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 image->rgba.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    entry.info.width = image->width;
    entry.info.height = image->height;
    entry.info.meta = loadSpriteMeta(file);
    applyFilter(entry.id, entry.info.meta.filter);
    return true;
}

TextureId AssetCache::texture2D(const std::string& path)
{
    if (const auto it = m_textures2D.find(path); it != m_textures2D.end())
        return it->second.id;
    Texture2D& entry = m_textures2D[path];
    if (loadTexture2D(path, entry))
        m_spriteInfo[entry.id] = &entry.info;
    return entry.id;
}

const AssetCache::SpriteInfo* AssetCache::spriteInfo(TextureId id) const
{
    const auto it = m_spriteInfo.find(id);
    return it == m_spriteInfo.end() ? nullptr : it->second;
}

SpriteMeta AssetCache::spriteMeta(const std::string& path)
{
    const SpriteInfo* info = spriteInfo(texture2D(path));
    return info ? info->meta : loadSpriteMeta(resourcePath(path));
}

void AssetCache::setSpriteMeta(const std::string& path, const SpriteMeta& meta, bool save)
{
    const std::string file = resourcePath(path);
    if (save)
        saveSpriteMeta(file, meta);
    const auto it = m_textures2D.find(path);
    if (it == m_textures2D.end() || it->second.id == InvalidTexture)
        return;
    it->second.info.meta = meta;
    it->second.metaTime = fileTime(spriteMetaPath(file));
    applyFilter(it->second.id, meta.filter);
}

void AssetCache::pollTextureChanges()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastTexturePoll < std::chrono::milliseconds(500))
        return;
    m_lastTexturePoll = now;
    for (auto& [path, entry] : m_textures2D) {
        const std::string file = resourcePath(path);
        if (fileTime(file) == entry.imageTime && fileTime(spriteMetaPath(file)) == entry.metaTime)
            continue;
        const TextureId before = entry.id;
        if (loadTexture2D(path, entry) && before == InvalidTexture)
            m_spriteInfo[entry.id] = &entry.info;
    }
}

AssetCache::AssetCache() = default;
AssetCache::~AssetCache() = default;

Font* AssetCache::font(const std::string& path)
{
    const std::string& ref = path.empty() ? std::string(DefaultFont) : path;
    if (const auto it = m_fonts.find(ref); it != m_fonts.end())
        return it->second.get();
    std::unique_ptr<Font> font = Font::load(resourcePath(ref));
    if (!font)
        std::fprintf(stderr, "Can't load font %s\n", ref.c_str());
    return (m_fonts[ref] = std::move(font)).get();
}

void AssetCache::releaseTextures()
{
    m_fonts.clear();
    for (auto& [path, entry] : m_textures2D)
        glDeleteTextures(1, &entry.id);
    m_textures2D.clear();
    m_spriteInfo.clear();
}

void AssetCache::rename(const std::string& from, const std::string& to)
{
    renameKeys(m_textures2D, from, to);  // moves the values, so the info pointers follow
    m_spriteInfo.clear();
    for (const auto& [path, entry] : m_textures2D) {
        if (entry.id != InvalidTexture)
            m_spriteInfo[entry.id] = &entry.info;
    }
    renameKeys(m_models, from, to);
    renameKeys(m_animations, from, to);
    renameKeys(m_animFiles, from, to);
    ++m_clipVersion;
    renameKeys(m_textures, from, to);
    renameKeys(m_materials, from, to);
    for (auto& [path, entry] : m_materials) {
        if (pathIsUnder(entry.asset.baseMap, from))
            entry.asset.baseMap = rebasePath(entry.asset.baseMap, from, to);
    }
}
