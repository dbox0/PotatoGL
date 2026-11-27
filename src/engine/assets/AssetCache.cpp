#include "AssetCache.h"

#include "engine/assets/Model.h"
#include "engine/core/Paths.h"

#include <cstdio>

ModelId AssetCache::model(const std::string& path)
{
    if (const auto it = m_models.find(path); it != m_models.end())
        return it->second;

    ModelId id = InvalidModel;
    if (m_renderer) {
        if (const std::optional<ModelData> data = loadModel(resourcePath(path)))
            id = m_renderer->addModel(*data);
    }
    if (id == InvalidModel)
        std::fprintf(stderr, "Failed to load model '%s'\n", path.c_str());
    m_models.emplace(path, id);
    return id;
}
