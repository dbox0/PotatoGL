#pragma once

#include "engine/gfx/Shader.h"

#include <entt/entity/entity.hpp>
#include <glad/gl.h>
#include <glm/glm.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

class RenderTarget;
class Scene;
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
        Image = 0,
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
    glm::vec2 m_targetSize{1.0f};
    Stats m_stats;
    std::chrono::steady_clock::time_point m_lastShaderPoll;

    UIRenderer() = default;
    void drawElement(const Scene& scene, entt::entity e);
    void drawShape(const UIShape& shape, const UIRect& rect);

public:
    // nullptr if the shader fails to load
    static std::unique_ptr<UIRenderer> create();
    ~UIRenderer();

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    static std::uint32_t packColor(const glm::vec4& color);

    void begin(glm::vec2 targetSize);
    void drawCanvas(const Scene& scene, entt::entity canvas);  // walks the UIRects
    void drawCanvases(const Scene& scene);                       // every canvas, in sortOrder
    // texture 0 for untextured modes; never breaks a batch
    void addTriangles(GLuint texture, const Vertex* vertices, std::size_t count);
    // target = nullptr: the window's framebuffer
    void end(const RenderTarget* target, int width, int height);

    void pollShaderChanges();
    Stats stats() const { return m_stats; }  // last end()
};
