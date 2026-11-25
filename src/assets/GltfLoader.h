#pragma once

#include "../gfx/Vertex.h"
#include "Image.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::optional<ImageData> baseColor;
};

// Merges every triangle primitive into one mesh with node transforms baked in.
// baseColor comes from the first material that has one.
std::optional<MeshData> loadGltf(const std::string& path);
