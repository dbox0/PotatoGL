#pragma once

#include <string_view>

// Project assets live under this resource folder. References to them (MeshRenderer::mesh,
// material maps, ...) are resource-relative: "assets/Models/Duck.glb".
inline constexpr std::string_view AssetsFolder = "assets";

enum class AssetKind
{
    None,
    Folder,
    Scene,
    Prefab,
    Material,
    Texture,
    Model,
    Script,
    Shader,
    Other,
};

// By extension, case-insensitive; Other for unknown files. Never returns Folder.
AssetKind assetKindOf(std::string_view path);
const char* assetKindName(AssetKind kind);
