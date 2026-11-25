#pragma once

#include <string>
#include <string_view>

// Path to a file shipped next to the executable, e.g. resourcePath("shaders/mesh.vert").
// Debug builds read straight from the source tree instead, so shader edits hot reload.
std::string resourcePath(std::string_view relative);

// "models/duck.gltf" -> "models/", "duck.gltf" -> ""
std::string directoryOf(const std::string& path);
