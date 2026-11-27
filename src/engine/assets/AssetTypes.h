#pragma once

#include <cstdint>
#include <string_view>

// Project assets live under this resource folder. References to them (MeshRenderer::mesh,
// material maps, ...) are resource-relative: "assets/Models/Duck.glb".
inline constexpr std::string_view AssetsFolder = "assets";

// A standalone GL_TEXTURE_2D (sprites, UI images); the GL name, stable across hot reloads
using TextureId = std::uint32_t;
inline constexpr TextureId InvalidTexture = 0;

enum class AssetKind
{
    None,
    Folder,
    Scene,
    Prefab,
    Material,
    Texture,
    Model,
    Animation,  // .anim, and "model#clip" references to clips embedded in models
    Pose,
    Script,
    Shader,
    Other,
};

// By extension, case-insensitive; Other for unknown files. Never returns Folder.
AssetKind assetKindOf(std::string_view path);
const char* assetKindName(AssetKind kind);
