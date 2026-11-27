#pragma once

#include "engine/assets/AssetTypes.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// File operations on the project's assets folder. Paths are absolute std::filesystem
// paths; "refs" are the resource-relative strings scenes and materials store
// ("assets/Models/Duck.glb"). Nothing here leaves the assets folder.
namespace assetops
{
namespace fs = std::filesystem;

fs::path root();
std::string toRef(const fs::path& path);
fs::path fromRef(const std::string& ref);
// "assets/Models/Duck.glb" -> "Assets/Models/Duck.glb", for display
std::string displayPath(const std::string& ref);

// path is the root or inside it (lexically, after normalizing)
bool isInsideRoot(const fs::path& path);
// Folders are AssetKind::Folder, files go by extension
AssetKind kindOf(const fs::path& path);

// dir/stem.ext, or dir/"stem 1".ext, "stem 2" ... whichever is free first
fs::path uniquePath(const fs::path& dir, const std::string& stem, const std::string& ext);
// A file or folder name without "/", "\", ".." and the like
bool validName(const std::string& name);

std::optional<fs::path> createFolder(const fs::path& dir, std::string* error);
std::optional<fs::path> createMaterial(const fs::path& dir, std::string* error);
bool writeText(const fs::path& path, const std::string& text, std::string* error);
std::optional<std::string> readText(const fs::path& path, std::size_t maxBytes = SIZE_MAX);

// Fails rather than overwrite, or move a folder into itself
bool move(const fs::path& from, const fs::path& to, std::string* error);
std::optional<fs::path> duplicate(const fs::path& path, std::string* error);
bool remove(const fs::path& path, std::string* error);

// Copies an outside file or folder into dir under a free name. A .gltf brings the
// buffers and images it references. Returns the copy.
std::optional<fs::path> import(const fs::path& source, const fs::path& dir, std::string* error);

// Points every reference to from (or something under it) in the project's .scene,
// .prefab and .mat files at to. Returns the number of files changed.
int rewriteReferences(const std::string& fromRef, const std::string& toRef);

// Every asset of kind under the root, as refs, sorted. Folders for AssetKind::Folder
// (the root included).
std::vector<std::string> findAssets(AssetKind kind);
}  // namespace assetops
