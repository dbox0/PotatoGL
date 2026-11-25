#include "Model.h"

#include "engine/assets/FbxLoader.h"
#include "engine/assets/GltfLoader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

std::optional<ModelData> loadModel(const std::string& path)
{
    const auto dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    if (ext == "gltf" || ext == "glb")
        return loadGltf(path);
    if (ext == "fbx" || ext == "obj")
        return loadFbx(path);

    std::fprintf(stderr, "Unsupported model format: %s\n", path.c_str());
    return std::nullopt;
}
