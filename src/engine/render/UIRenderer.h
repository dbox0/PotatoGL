#pragma once

#include "engine/gfx/Shader.h"

#include <entt/entity/entity.hpp>
#include <glad/gl.h>
#include <glm/glm.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

class AssetCache;
class RenderTarget;
class Scene;
struct UIImage;
struct UIText;
struct UIRect;
struct UIShape;

// Screen-space UI: batched quads, one shader, drawn after tonemap into the display (LDR)
// framebuffer with GL_FRAMEBUFFER_SRGB off, so colors are sRGB and blend like ImGui's.
// Premultiplied alpha. Lay out the scene (uiLayoutSystem) for the same size first.
class UIRenderer
{
public:
    enum Mode : std::uint32_t
    {
        Image = 0,  // the texture is sRGB-decoded on sampling, so its rgb is encoded back
        Shape = 1,  // rounded box SDF; uv = offset from the centre, params = half size, radius, border
        Text = 2,
    };

    struct Vertex
    {
        glm::vec2 pos;  // target pixels, top-left origin
        glm::vec2 uv;
        std::uint32_t color;   // RGBA8, sRGB
        std::uint32_t color2;  // RGBA8, sRGB: the border
        glm::vec4 params;
        std::uint32_t mode;
    };

    struct Stats
    {
        std::size_t vertices = 0;
        std::size_t batches = 0;
    };

private:
    struct Batch
    {
        GLuint texture;  // 0: none needed yet, joins any
        GLint first;
        GLsizei count;
    };

    Shader m_shader;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_white = 0;
    GLint m_targetSizeLocation = -1;
    std::vector<Vertex> m_vertices;
    std::vector<Batch> m_batches;
    std::vector<Vertex> m_textVertices;  // scratch
    glm::vec2 m_targetSize{1.0f};
    Stats m_stats;
    Stats m_frame;      // since beginFrame()
    Stats m_lastFrame;
    AssetCache* m_assets = nullptr;
    std::chrono::steady_clock::time_point m_lastShaderPoll;

    UIRenderer() = default;
    void drawElement(Scene& scene, entt::entity e);
    void drawShape(const UIShape& shape, const UIRect& rect, const glm::vec4& tint);
    void drawImage(const UIImage& image, const UIRect& rect, const glm::vec4& tint);
    void drawText(Scene& scene, entt::entity e, const UIText& text, const UIRect& rect, const glm::vec4& tint);
    // Quad from local min to max with uvs from uvMin to uvMax
    void imageQuad(GLuint texture, const UIRect& rect, glm::vec2 min, glm::vec2 max, glm::vec2 uvMin, glm::vec2 uvMax,
                   std::uint32_t color);

public:
    // nullptr if the shader fails to load
    static std::unique_ptr<UIRenderer> create();
    ~UIRenderer();

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    static std::uint32_t packColor(const glm::vec4& color);

    void setAssets(AssetCache* assets) { m_assets = assets; }  // fonts; no text without it

    void beginFrame();  // once per frame, for frameStats()
    void begin(glm::vec2 targetSize);
    // Walks the UIRects; lays out text, so not const
    void drawCanvas(Scene& scene, entt::entity canvas);
    void drawCanvases(Scene& scene);  // every canvas, in sortOrder
    // texture 0 for untextured modes; never breaks a batch
    void addTriangles(GLuint texture, const Vertex* vertices, std::size_t count);
    // target = nullptr: the window's framebuffer
    void end(const RenderTarget* target, int width, int height);

    void pollShaderChanges();
    Stats stats() const { return m_stats; }           // last end()
    Stats frameStats() const { return m_lastFrame; }  // every end() of the previous frame
};
