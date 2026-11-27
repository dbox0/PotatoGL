#pragma once

#include "engine/assets/AssetTypes.h"
#include "engine/gfx/RenderTarget.h"

#include <glad/gl.h>

#include <deque>
#include <filesystem>
#include <string>
#include <unordered_map>

class AssetCache;
class Renderer;

// Small previews for the Project view and the inspector: textures are downscaled, models and
// materials (on a sphere) are rendered. Built a few per frame in process(); get() returns
// nothing until then. Entries are rebuilt when the file's time changes or on invalidate().
class ThumbnailCache
{
public:
    static constexpr int Size = 192;

    struct Image
    {
        GLuint texture = 0;
        int width = 0, height = 0;  // thumbnail pixels, fits in Size x Size
        int sourceWidth = 0, sourceHeight = 0;  // textures: the original size
        bool flipY = false;  // rendered previews are bottom-up

        explicit operator bool() const { return texture != 0; }
    };

private:
    struct Entry
    {
        Image image;
        AssetKind kind = AssetKind::None;
        std::filesystem::file_time_type stamp;
        bool queued = false;
        bool failed = false;
    };

    std::unordered_map<std::string, Entry> m_entries;  // by ref
    std::deque<std::string> m_queue;
    RenderTarget m_target;

    void build(const std::string& ref, Entry& entry, Renderer& renderer, AssetCache& assets);

public:
    ThumbnailCache() = default;
    ~ThumbnailCache();

    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    // kind must be Texture, Material or Model; others never get one
    static bool supports(AssetKind kind);
    // stamp: the file's last write time, so edits made outside the editor show up
    Image get(const std::string& ref, AssetKind kind, std::filesystem::file_time_type stamp);
    // Rebuild ref and everything under it (a renamed or edited asset)
    void invalidate(const std::string& ref);

    // GL work; budget = how many to build this frame
    void process(Renderer& renderer, AssetCache& assets, int budget);
    void release();  // before the GL context goes
};
