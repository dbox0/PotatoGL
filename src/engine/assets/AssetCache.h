#pragma once

#include "engine/assets/Material.h"
#include "engine/assets/SpriteMeta.h"
#include "engine/render/Renderer.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Loads each asset path once and hands out its renderer handle. Failures are cached too,
// so a bad path costs one attempt, not one per frame. Paths are resource-relative.
class Font;

class AssetCache
{
public:
    struct SpriteInfo
    {
        int width = 0;
        int height = 0;
        SpriteMeta meta;
    };

private:
    struct Texture2D
    {
        TextureId id = InvalidTexture;  // InvalidTexture: failed to load
        std::filesystem::file_time_type imageTime;
        std::filesystem::file_time_type metaTime;
        SpriteInfo info;
    };

    struct MaterialEntry
    {
        MaterialId id = InvalidMaterial;
        MaterialAsset asset;
    };

    struct Animation
    {
        std::shared_ptr<const SkeletonData> skeleton;
        std::vector<std::shared_ptr<const ClipData>> clips;
    };

    Renderer* m_renderer = nullptr;
    std::unordered_map<std::string, ModelId> m_models;
    std::unordered_map<std::string, Animation> m_animations;  // by model path; skinned models only
    std::unordered_map<std::string, std::shared_ptr<const ClipData>> m_animFiles;  // .anim; nullptr = failed
    std::uint64_t m_clipVersion = 1;
    std::unordered_map<std::string, GLuint> m_textures;
    std::unordered_map<std::string, MaterialEntry> m_materials;
    std::unordered_map<std::string, Texture2D> m_textures2D;
    std::unordered_map<TextureId, const SpriteInfo*> m_spriteInfo;  // into m_textures2D's nodes, which don't move
    std::chrono::steady_clock::time_point m_lastTexturePoll;
    std::unordered_map<std::string, std::unique_ptr<Font>> m_fonts;  // nullptr = failed

    bool loadTexture2D(const std::string& path, Texture2D& entry);

    Material toRenderer(const MaterialAsset& asset);

public:
    void setRenderer(Renderer* renderer) { m_renderer = renderer; }

    // path is relative to the resource dir, or "builtin:<name>" (Primitives.h); InvalidModel if it can't be loaded
    ModelId model(const std::string& path);
    // A skinned model's skeleton, loading the model first; nullptr if it has none
    std::shared_ptr<const SkeletonData> skeleton(const std::string& modelPath);
    // "model#clip" for a clip embedded in a model, or an .anim file's baked clip; nullptr if
    // there's no such clip
    std::shared_ptr<const ClipData> clip(const std::string& ref);
    // An .anim was written (or deleted: nullptr), so the next clip() returns this
    void setClip(const std::string& path, std::shared_ptr<const ClipData> clip);
    // Changes whenever setClip() or a rename changes what clip() returns; Animators re-resolve then
    std::uint64_t clipVersion() const { return m_clipVersion; }
    // References to every clip embedded in the model
    std::vector<std::string> clipRefs(const std::string& modelPath);

    // Texture array layer; 0 (white) if it can't be loaded
    GLuint texture(const std::string& path);
    // A standalone GL_TEXTURE_2D: sRGB8_ALPHA8, mipmapped, clamped, filtered per its .meta.
    // InvalidTexture if it can't be loaded. Reloaded in place when the image or .meta changes.
    TextureId texture2D(const std::string& path);
    const SpriteInfo* spriteInfo(TextureId id) const;
    // Loads the texture first; the defaults if it can't be loaded
    SpriteMeta spriteMeta(const std::string& path);
    // Applies right away; save also writes the .meta
    void setSpriteMeta(const std::string& path, const SpriteMeta& meta, bool save);
    // .ttf / .otf; empty = DefaultFont. nullptr if it can't be loaded.
    Font* font(const std::string& path);
    static constexpr const char* DefaultFont = "assets/Fonts/NotoSans-Regular.ttf";

    void pollTextureChanges();  // cheap to call every frame, checks file times twice a second
    void releaseTextures();     // and fonts; before the GL context is destroyed

    AssetCache();
    ~AssetCache();
    AssetCache(const AssetCache&) = delete;
    AssetCache& operator=(const AssetCache&) = delete;

    // A .mat file; InvalidMaterial if it can't be loaded
    MaterialId material(const std::string& path);

    // The material's current values, loading it first; nullptr if it can't be loaded
    const MaterialAsset* materialAsset(const std::string& path);
    // Applies new values to the renderer right away; saving the file is up to the caller
    void setMaterialAsset(const std::string& path, const MaterialAsset& asset);

    // A file or folder moved: what was loaded from (or under) from now lives at to, and
    // loaded materials' texture references follow it
    void rename(const std::string& from, const std::string& to);
};

// "assets/A" covers "assets/A" itself, "assets/A/b.png" and the clip "assets/A#Run", not "assets/AB"
bool pathIsUnder(const std::string& path, const std::string& folder);
// path with the leading from replaced by to; pathIsUnder(path, from) must hold
std::string rebasePath(const std::string& path, const std::string& from, const std::string& to);

// Clip references are "model#clip"; the model part of a ref without '#' is the ref itself
std::string clipModelPath(const std::string& ref);
