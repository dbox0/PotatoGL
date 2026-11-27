#pragma once

#include "engine/render/Renderer.h"

#include <string>
#include <unordered_map>

// Loads each model path once and hands out its ModelId. Failures are cached too,
// so a bad path costs one attempt, not one per frame.
class AssetCache
{
private:
    Renderer* m_renderer = nullptr;
    std::unordered_map<std::string, ModelId> m_models;

public:
    void setRenderer(Renderer* renderer) { m_renderer = renderer; }

    // path is relative to the resource dir, or "builtin:<name>" (Primitives.h); InvalidModel if it can't be loaded
    ModelId model(const std::string& path);
};
