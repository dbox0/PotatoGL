#include "AssetTypes.h"

#include <algorithm>
#include <cctype>
#include <string>

AssetKind assetKindOf(std::string_view path)
{
    const std::size_t slash = path.find_last_of("/\\");
    const std::string_view file = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = file.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0)
        return AssetKind::Other;
    std::string ext(file.substr(dot + 1));
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    if (ext == "scene")
        return AssetKind::Scene;
    if (ext == "prefab")
        return AssetKind::Prefab;
    if (ext == "mat")
        return AssetKind::Material;
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "bmp" || ext == "psd" || ext == "gif")
        return AssetKind::Texture;
    if (ext == "glb" || ext == "gltf" || ext == "fbx" || ext == "obj")
        return AssetKind::Model;
    if (ext == "anim")
        return AssetKind::Animation;
    if (ext == "pose")
        return AssetKind::Pose;
    if (ext == "cpp" || ext == "h" || ext == "hpp" || ext == "cc" || ext == "c")
        return AssetKind::Script;
    if (ext == "ttf" || ext == "otf")
        return AssetKind::Font;
    if (ext == "vert" || ext == "frag" || ext == "glsl" || ext == "comp" || ext == "geom")
        return AssetKind::Shader;
    return AssetKind::Other;
}

const char* assetKindName(AssetKind kind)
{
    switch (kind) {
    case AssetKind::None:     return "None";
    case AssetKind::Folder:   return "Folder";
    case AssetKind::Scene:    return "Scene";
    case AssetKind::Prefab:   return "Prefab";
    case AssetKind::Material: return "Material";
    case AssetKind::Texture:  return "Texture";
    case AssetKind::Model:    return "Model";
    case AssetKind::Animation: return "Animation Clip";
    case AssetKind::Pose:     return "Pose";
    case AssetKind::Script:   return "Script";
    case AssetKind::Shader:   return "Shader";
    case AssetKind::Font:     return "Font";
    case AssetKind::Other:    return "File";
    }
    return "File";
}
