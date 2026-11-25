#pragma once

#include "engine/assets/Image.h"
#include "engine/gfx/Vertex.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// One mesh per primitive / material part, node transforms baked in
struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    int texture = -1;  // index into ModelData::textures, -1 = untextured
};

struct ModelData
{
    std::vector<MeshData> meshes;
    std::vector<ImageData> textures;
};

// Picks the loader from the extension: .gltf/.glb, .fbx/.obj
std::optional<ModelData> loadModel(const std::string& path);
