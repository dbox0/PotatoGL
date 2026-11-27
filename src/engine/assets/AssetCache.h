#pragma once

#include "engine/assets/Material.h"
#include "engine/render/Renderer.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Loads each asset path once and hands out its renderer handle. Failures are cached too,
// so a bad path costs one attempt, not one per frame. Paths are resource-relative.
class AssetCache
{
private:
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
    std::unordered_map<std::string, GLuint> m_textures;
    std::unordered_map<std::string, MaterialEntry> m_materials;

    Material toRenderer(const MaterialAsset& asset);

public:
    void setRenderer(Renderer* renderer) { m_renderer = renderer; }

    // path is relative to the resource dir, or "builtin:<name>" (Primitives.h); InvalidModel if it can't be loaded
    ModelId model(const std::string& path);
    // A skinned model's skeleton, loading the model first; nullptr if it has none
    std::shared_ptr<const SkeletonData> skeleton(const std::string& modelPath);
    // "model#clip" for a clip embedded in a model; nullptr if there's no such clip
    std::shared_ptr<const ClipData> clip(const std::string& ref);
    // References to every clip embedded in the model
    std::vector<std::string> clipRefs(const std::string& modelPath);

    // Texture array layer; 0 (white) if it can't be loaded
    GLuint texture(const std::string& path);
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
