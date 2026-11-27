#include "AssetCache.h"

#include "engine/assets/Model.h"
#include "engine/assets/Primitives.h"
#include "engine/core/Paths.h"

#include <cstdio>

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
