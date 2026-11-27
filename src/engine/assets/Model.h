#pragma once

#include "engine/anim/Clip.h"
#include "engine/anim/Skeleton.h"
#include "engine/assets/Image.h"
#include "engine/gfx/Vertex.h"

#include <glm/glm.hpp>
#include <glm/ext/vector_uint4_sized.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Imported material, linear values; becomes the model's default for the meshes using it
struct MaterialData
{
    glm::vec4 baseColor{1.0f};
    glm::vec3 emission{0.0f};
    float metallic = 0.0f;
    float smoothness = 0.5f;
    int texture = -1;  // index into ModelData::textures, -1 = untextured
};

// One mesh per primitive / material part, node transforms baked in. Skinned meshes have
// joints and weights (one per vertex) and keep their own space: the skin places them.
struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    int material = -1;  // index into ModelData::materials, -1 = the renderer's default
    std::vector<glm::u8vec4> joints;   // skeleton joint indices
    std::vector<glm::u8vec4> weights;  // unorm8, sorted descending, sum to exactly 255

    bool skinned() const { return !joints.empty(); }
};

struct ModelData
{
    std::vector<MeshData> meshes;
    std::vector<MaterialData> materials;
    std::vector<ImageData> textures;
    std::optional<SkeletonData> skeleton;  // every skinned mesh of the model shares it
    std::vector<ClipData> clips;           // bound to skeleton by joint name
};

// Picks the loader from the extension: .gltf/.glb, .fbx/.obj
std::optional<ModelData> loadModel(const std::string& path);
