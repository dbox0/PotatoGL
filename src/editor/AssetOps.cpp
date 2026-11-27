#include "AssetOps.h"

#include "engine/assets/Material.h"
#include "engine/core/Paths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace assetops
{
static std::string errorText(const std::error_code& ec)
{
    return ec.message();
}

fs::path root()
{
    return fs::path(resourcePath(AssetsFolder)).lexically_normal();
}

std::string toRef(const fs::path& path)
{
    const fs::path relative = path.lexically_normal().lexically_relative(root());
    if (relative.empty() || relative == ".")
        return std::string(AssetsFolder);
    return std::string(AssetsFolder) + "/" + relative.generic_string();
}

fs::path fromRef(const std::string& ref)
{
    return fs::path(resourcePath(ref)).lexically_normal();
}

std::string displayPath(const std::string& ref)
{
    if (ref.starts_with(AssetsFolder))
        return "Assets" + ref.substr(AssetsFolder.size());
    return ref;
}

bool isInsideRoot(const fs::path& path)
{
    const fs::path relative = path.lexically_normal().lexically_relative(root());
    return !relative.empty() && *relative.begin() != "..";
}

AssetKind kindOf(const fs::path& path)
{
    std::error_code ec;
    if (fs::is_directory(path, ec))
        return AssetKind::Folder;
    return assetKindOf(path.generic_string());
}

fs::path uniquePath(const fs::path& dir, const std::string& stem, const std::string& ext)
{
    std::error_code ec;
    fs::path candidate = dir / (stem + ext);
    for (int i = 1; fs::exists(candidate, ec); ++i)
        candidate = dir / (stem + " " + std::to_string(i) + ext);
    return candidate;
}

bool validName(const std::string& name)
{
    if (name.empty() || name == "." || name == ".." || name.front() == ' ' || name.back() == ' ')
        return false;
    return name.find_first_of("/\\:*?\"<>|") == std::string::npos;
}

std::optional<fs::path> createFolder(const fs::path& dir, std::string* error)
{
    const fs::path path = uniquePath(dir, "New Folder", "");
    std::error_code ec;
    if (!fs::create_directory(path, ec)) {
        if (error)
            *error = "can't create folder: " + errorText(ec);
        return std::nullopt;
    }
    return path;
}

std::optional<fs::path> createMaterial(const fs::path& dir, std::string* error)
{
    const fs::path path = uniquePath(dir, "New Material", ".mat");
    if (!saveMaterialFile(path.string(), MaterialAsset{}, error))
        return std::nullopt;
    return path;
}

bool writeText(const fs::path& path, const std::string& text, std::string* error)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary);
    if (file)
        file << text;
    if (!file) {
        if (error)
            *error = "can't write " + path.filename().string();
        return false;
    }
    return true;
}

std::optional<std::string> readText(const fs::path& path, std::size_t maxBytes)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::nullopt;
    if (maxBytes == SIZE_MAX) {
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
    std::string text(maxBytes, '\0');
    file.read(text.data(), static_cast<std::streamsize>(maxBytes));
    text.resize(static_cast<std::size_t>(file.gcount()));
    return text;
}

bool move(const fs::path& from, const fs::path& to, std::string* error)
{
    const auto fail = [&](std::string message) {
        if (error)
            *error = std::move(message);
        return false;
    };
    if (!isInsideRoot(from) || !isInsideRoot(to) || from.lexically_normal() == root())
        return fail("can't move outside the Assets folder");
    if (from.lexically_normal() == to.lexically_normal())
        return true;
    const fs::path inside = to.lexically_normal().lexically_relative(from.lexically_normal());
    if (!inside.empty() && *inside.begin() != "..")
        return fail("can't move a folder into itself");
    std::error_code ec;
    if (fs::exists(to, ec))
        return fail(to.filename().string() + " already exists");
    fs::rename(from, to, ec);
    if (ec)
        return fail("can't move " + from.filename().string() + ": " + errorText(ec));
    const fs::path meta = from.string() + ".meta";
    if (fs::is_regular_file(meta, ec))
        fs::rename(meta, to.string() + ".meta", ec);
    return true;
}

std::optional<fs::path> duplicate(const fs::path& path, std::string* error)
{
    std::error_code ec;
    const bool folder = fs::is_directory(path, ec);
    const std::string stem = folder ? path.filename().string() : path.stem().string();
    const fs::path copy = uniquePath(path.parent_path(), stem, folder ? "" : path.extension().string());
    fs::copy(path, copy, fs::copy_options::recursive, ec);
    if (ec) {
        if (error)
            *error = "can't duplicate " + path.filename().string() + ": " + errorText(ec);
        return std::nullopt;
    }
    if (const fs::path meta = path.string() + ".meta"; !folder && fs::is_regular_file(meta, ec))
        fs::copy_file(meta, copy.string() + ".meta", ec);
    return copy;
}

bool remove(const fs::path& path, std::string* error)
{
    if (!isInsideRoot(path) || path.lexically_normal() == root()) {
        if (error)
            *error = "can't delete outside the Assets folder";
        return false;
    }
    std::error_code ec;
    fs::remove_all(path, ec);
    if (!ec)
        fs::remove(path.string() + ".meta", ec);
    if (ec) {
        if (error)
            *error = "can't delete " + path.filename().string() + ": " + errorText(ec);
        return false;
    }
    return true;
}

// Relative uris of a .gltf's buffers and images
static std::vector<std::string> gltfDependencies(const fs::path& gltf)
{
    std::vector<std::string> uris;
    const std::optional<std::string> text = readText(gltf);
    if (!text)
        return uris;
    const nlohmann::json root = nlohmann::json::parse(*text, nullptr, false);
    if (!root.is_object())
        return uris;
    for (const char* key : {"buffers", "images"}) {
        const auto list = root.find(key);
        if (list == root.end() || !list->is_array())
            continue;
        for (const auto& item : *list) {
            const auto uri = item.find("uri");
            if (uri == item.end() || !uri->is_string())
                continue;
            const std::string value = uri->get<std::string>();
            if (value.starts_with("data:") || value.find("://") != std::string::npos)
                continue;
            uris.push_back(value);
        }
    }
    return uris;
}

std::optional<fs::path> import(const fs::path& source, const fs::path& dir, std::string* error)
{
    std::error_code ec;
    const bool folder = fs::is_directory(source, ec);
    if (!folder && !fs::is_regular_file(source, ec)) {
        if (error)
            *error = "can't import " + source.filename().string();
        return std::nullopt;
    }
    const fs::path target = uniquePath(dir, folder ? source.filename().string() : source.stem().string(),
                                       folder ? "" : source.extension().string());
    fs::copy(source, target, fs::copy_options::recursive, ec);
    if (ec) {
        if (error)
            *error = "can't import " + source.filename().string() + ": " + errorText(ec);
        return std::nullopt;
    }

    // Companions keep their relative paths, so the copy resolves them the same way
    if (!folder && assetKindOf(source.generic_string()) == AssetKind::Model && source.extension() == ".gltf") {
        for (const std::string& uri : gltfDependencies(source)) {
            const fs::path relative = fs::path(uri).lexically_normal();
            if (relative.is_absolute() || relative.empty() || *relative.begin() == "..")
                continue;
            const fs::path to = dir / relative;
            if (fs::exists(to, ec))
                continue;
            fs::create_directories(to.parent_path(), ec);
            fs::copy_file(source.parent_path() / relative, to, ec);
        }
    }
    return target;
}

static bool replaceAll(std::string& text, const std::string& from, const std::string& to)
{
    bool changed = false;
    for (std::size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size())) {
        text.replace(pos, from.size(), to);
        changed = true;
    }
    return changed;
}

int rewriteReferences(const std::string& fromRef, const std::string& toRef)
{
    int files = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root(), fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec))
            continue;
        const AssetKind kind = assetKindOf(it->path().generic_string());
        if (kind != AssetKind::Scene && kind != AssetKind::Prefab && kind != AssetKind::Material &&
            kind != AssetKind::Animation && kind != AssetKind::Pose)
            continue;
        std::optional<std::string> text = readText(it->path());
        if (!text)
            continue;
        // JSON strings: the exact path, or a path inside the moved folder
        const bool exact = replaceAll(*text, "\"" + fromRef + "\"", "\"" + toRef + "\"");
        const bool nested = replaceAll(*text, "\"" + fromRef + "/", "\"" + toRef + "/");
        const bool clips = replaceAll(*text, "\"" + fromRef + "#", "\"" + toRef + "#");  // "model#clip"
        if ((exact || nested || clips) && writeText(it->path(), *text, nullptr))
            ++files;
    }
    return files;
}

std::vector<std::string> findAssets(AssetKind kind)
{
    std::vector<std::string> refs;
    if (kind == AssetKind::Folder)
        refs.push_back(toRef(root()));
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root(), fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.starts_with('.')) {
            if (it->is_directory(ec))
                it.disable_recursion_pending();
            continue;
        }
        if (kindOf(it->path()) == kind)
            refs.push_back(toRef(it->path()));
    }
    std::sort(refs.begin(), refs.end());
    return refs;
}
}  // namespace assetops
