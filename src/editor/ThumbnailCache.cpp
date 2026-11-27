#include "ThumbnailCache.h"

#include "engine/assets/AssetCache.h"
#include "engine/assets/Image.h"
#include "engine/core/Paths.h"
#include "engine/render/Renderer.h"

#include <algorithm>
#include <vector>

ThumbnailCache::~ThumbnailCache()
{
    release();
}

void ThumbnailCache::release()
{
    for (auto& [ref, entry] : m_entries) {
        if (entry.image.texture)
            glDeleteTextures(1, &entry.image.texture);
    }
    m_entries.clear();
    m_queue.clear();
    m_target.release();
}

bool ThumbnailCache::supports(AssetKind kind)
{
    return kind == AssetKind::Texture || kind == AssetKind::Material || kind == AssetKind::Model;
}

ThumbnailCache::Image ThumbnailCache::get(const std::string& ref, AssetKind kind,
                                          std::filesystem::file_time_type stamp)
{
    if (!supports(kind))
        return {};
    Entry& entry = m_entries[ref];
    if (entry.kind != kind || entry.stamp != stamp) {
        entry.kind = kind;
        entry.stamp = stamp;
        entry.failed = false;
        if (!entry.queued) {
            entry.queued = true;
            m_queue.push_back(ref);
        }
    }
    return entry.image;
}

void ThumbnailCache::invalidate(const std::string& ref)
{
    for (auto& [path, entry] : m_entries) {
        if (pathIsUnder(path, ref) && !entry.queued) {
            entry.failed = false;
            entry.queued = true;
            m_queue.push_back(path);
        }
    }
}

// Box filter down to fit Size, keeping the aspect ratio
static std::vector<unsigned char> downscale(const ImageData& image, int& width, int& height)
{
    const float scale = std::min(1.0f, static_cast<float>(ThumbnailCache::Size) /
                                           static_cast<float>(std::max(image.width, image.height)));
    width = std::max(1, static_cast<int>(static_cast<float>(image.width) * scale));
    height = std::max(1, static_cast<int>(static_cast<float>(image.height) * scale));
    std::vector<unsigned char> out(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const int y0 = y * image.height / height;
        const int y1 = std::max(y0 + 1, (y + 1) * image.height / height);
        for (int x = 0; x < width; ++x) {
            const int x0 = x * image.width / width;
            const int x1 = std::max(x0 + 1, (x + 1) * image.width / width);
            unsigned sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; ++sy) {
                const unsigned char* row = image.rgba.data() + (static_cast<std::size_t>(sy) * image.width + x0) * 4;
                for (int sx = x0; sx < x1; ++sx, row += 4) {
                    for (int c = 0; c < 4; ++c)
                        sum[c] += row[c];
                }
            }
            const unsigned count = static_cast<unsigned>((y1 - y0) * (x1 - x0));
            unsigned char* dst = out.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            for (int c = 0; c < 4; ++c)
                dst[c] = static_cast<unsigned char>(sum[c] / count);
        }
    }
    return out;
}

// RGBA8, not sRGB: ImGui draws to a non-sRGB framebuffer, so the bytes go through as they are
static GLuint makeTexture(int width, int height, const unsigned char* rgba)
{
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
    if (rgba) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

void ThumbnailCache::build(const std::string& ref, Entry& entry, Renderer& renderer, AssetCache& assets)
{
    Image image;
    if (entry.kind == AssetKind::Texture) {
        if (const std::optional<ImageData> source = loadImage(resourcePath(ref))) {
            const std::vector<unsigned char> pixels = downscale(*source, image.width, image.height);
            image.texture = makeTexture(image.width, image.height, pixels.data());
            image.sourceWidth = source->width;
            image.sourceHeight = source->height;
        }
    } else {
        ModelId model = InvalidModel;
        MaterialId material = InvalidMaterial;
        glm::vec3 from(-0.55f, 0.45f, 1.0f);  // front left, a little above
        if (entry.kind == AssetKind::Material) {
            material = assets.material(ref);
            model = material == InvalidMaterial ? InvalidModel : assets.model("builtin:sphere");
            from = glm::vec3(0.0f, 0.25f, 1.0f);
        } else {
            model = assets.model(ref);
        }
        if (model != InvalidModel) {
            m_target.resize(Size, Size);
            renderer.renderPreview(model, material, m_target, from);
            image.width = image.height = Size;
            image.flipY = true;
            image.texture = makeTexture(Size, Size, nullptr);
            glCopyImageSubData(m_target.displayTexture(), GL_TEXTURE_2D, 0, 0, 0, 0, image.texture, GL_TEXTURE_2D, 0, 0,
                               0, 0, Size, Size, 1);
        }
    }

    if (entry.image.texture)
        glDeleteTextures(1, &entry.image.texture);
    entry.image = image;
    entry.failed = !image;
}

void ThumbnailCache::process(Renderer& renderer, AssetCache& assets, int budget)
{
    if (m_queue.empty())
        return;
    GpuScope scope(renderer.profiler(), "Thumbnails");
    while (budget > 0 && !m_queue.empty()) {
        const std::string ref = std::move(m_queue.front());
        m_queue.pop_front();
        const auto it = m_entries.find(ref);
        if (it == m_entries.end() || !it->second.queued)
            continue;
        it->second.queued = false;
        build(ref, it->second, renderer, assets);
        // Rendered previews cost far more than a small texture upload
        budget -= it->second.kind == AssetKind::Texture ? 1 : 2;
    }
}
