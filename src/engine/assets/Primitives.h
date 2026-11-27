#pragma once

#include "engine/assets/Model.h"

#include <optional>
#include <string_view>

// Generated meshes, referenced from MeshRenderer::mesh as "builtin:<name>".
// Unity sizes: unit cube, sphere of diameter 1, 10x10 plane, 1x1 quad facing +Z,
// cylinder and capsule 2 tall with radius 0.5.
inline constexpr std::string_view BuiltinMeshPrefix = "builtin:";

// name without the prefix; nullopt if unknown
std::optional<ModelData> makePrimitive(std::string_view name);
