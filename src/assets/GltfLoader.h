#pragma once

#include "../gfx/Vertex.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Merges every triangle primitive into one mesh with node transforms baked in
std::optional<MeshData> loadGltf(const std::string& path);
