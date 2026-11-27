#pragma once

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <string_view>

// A .mat file: the Lit shader's inputs. Colors are stored as picked (sRGB); AssetCache
// converts them to linear for the GPU.
//   { "version": 1, "shader": "Lit", "baseColor": [1, 1, 1, 1], "baseMap": "assets/Textures/Brick.png", ... }
struct MaterialAsset
{
    glm::vec4 baseColor{1.0f};  // alpha is unused for now
    std::string baseMap;        // resource-relative texture path, empty = none
    glm::vec2 tiling{1.0f};
    glm::vec2 offset{0.0f};
    float metallic = 0.0f;
    float smoothness = 0.5f;
    glm::vec3 emission{0.0f};
    float emissionIntensity = 1.0f;

    bool operator==(const MaterialAsset&) const = default;
};

std::string saveMaterial(const MaterialAsset& material);
// Missing fields keep their defaults
std::optional<MaterialAsset> loadMaterial(std::string_view text, std::string* error = nullptr);

bool saveMaterialFile(const std::string& path, const MaterialAsset& material, std::string* error = nullptr);
std::optional<MaterialAsset> loadMaterialFile(const std::string& path, std::string* error = nullptr);
