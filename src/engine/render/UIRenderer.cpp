#include "UIRenderer.h"

#include "engine/core/Paths.h"
#include "engine/gfx/RenderTarget.h"
#include "engine/scene/Components.h"
#include "engine/scene/Scene.h"
#include "engine/ui/UiComponents.h"
#include "engine/ui/UiLayout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>

std::unique_ptr<UIRenderer> UIRenderer::create()
{
    std::unique_ptr<UIRenderer> ui(new UIRenderer());
    if (!ui->m_shader.load(resourcePath("shaders/ui.vert"), resourcePath("shaders/ui.frag")))
        return nullptr;
    ui->m_targetSizeLocation = ui->m_shader.uniformLocation("uTargetSize");

    glGenVertexArrays(1, &ui->m_vao);
    glGenBuffers(1, &ui->m_vbo);
    glBindVertexArray(ui->m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, ui->m_vbo);
    constexpr GLsizei stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(Vertex, color)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(Vertex, color2)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, params)));
    glEnableVertexAttribArray(5);
    glVertexAttribIPointer(5, 1, GL_UNSIGNED_INT, stride, reinterpret_cast<void*>(offsetof(Vertex, mode)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const std::uint32_t white = 0xffffffffu;
    glGenTextures(1, &ui->m_white);
    glBindTexture(GL_TEXTURE_2D, ui->m_white);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    return ui;
}

UIRenderer::~UIRenderer()
{
    glDeleteTextures(1, &m_white);
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_vao);
}

std::uint32_t UIRenderer::packColor(const glm::vec4& color)
{
    const auto byte = [](float v) {
        return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
    };
    return byte(color.r) | byte(color.g) << 8 | byte(color.b) << 16 | byte(color.a) << 24;
}

void UIRenderer::begin(glm::vec2 targetSize)
{
    m_targetSize = targetSize;
    m_vertices.clear();
    m_batches.clear();
}

void UIRenderer::addTriangles(GLuint texture, const Vertex* vertices, std::size_t count)
{
    if (count == 0)
        return;
    if (m_batches.empty() || (texture != 0 && m_batches.back().texture != 0 && m_batches.back().texture != texture))
        m_batches.push_back({texture, static_cast<GLint>(m_vertices.size()), 0});
    Batch& batch = m_batches.back();
    if (batch.texture == 0)
        batch.texture = texture;
    m_vertices.insert(m_vertices.end(), vertices, vertices + count);
    batch.count += static_cast<GLsizei>(count);
}

void UIRenderer::drawCanvases(const Scene& scene)
{
    for (const entt::entity canvas : uiCanvases(scene))
        drawCanvas(scene, canvas);
}

void UIRenderer::drawCanvas(const Scene& scene, entt::entity canvas)
{
    if (scene.registry().all_of<Canvas, UIRect>(canvas))
        drawElement(scene, canvas);
}

// Same walk as uiLayoutSystem, so a stale UIRect (disabled since) is never drawn
void UIRenderer::drawElement(const Scene& scene, entt::entity e)
{
    const entt::registry& registry = scene.registry();
    const UIRect& rect = registry.get<UIRect>(e);
    if (const UIImage* image = registry.try_get<UIImage>(e))
        drawImage(*image, rect);
    if (const UIShape* shape = registry.try_get<UIShape>(e))
        drawShape(*shape, rect);
    scene.forEachChild(e, [&](entt::entity child) {
        if (!registry.all_of<Disabled>(child) && registry.all_of<RectTransform, UIRect>(child))
            drawElement(scene, child);
    });
}

void UIRenderer::drawShape(const UIShape& shape, const UIRect& rect)
{
    const glm::vec2 centre = (rect.min + rect.max) * 0.5f;
    glm::vec2 half = glm::abs(rect.max - rect.min) * 0.5f;
    float radius = 0.0f;
    switch (shape.shape) {
    case UIShapeType::Rect:
        break;
    case UIShapeType::RoundedRect:
        radius = std::clamp(shape.cornerRadius, 0.0f, std::min(half.x, half.y));
        break;
    case UIShapeType::Circle:
        half = glm::vec2(std::min(half.x, half.y));
        radius = half.x;
        break;
    case UIShapeType::Line: {
        const float w = std::max(shape.lineWidth, 0.0f) * 0.5f;
        half = glm::vec2(half.x + w, w);
        radius = w;
        break;
    }
    }
    if (half.x <= 0.0f || half.y <= 0.0f)
        return;

    const glm::vec2 axisX(rect.toScreen[0]), axisY(rect.toScreen[1]);
    const float pixels = std::min(glm::length(axisX), glm::length(axisY));
    if (pixels < 1e-6f)
        return;
    const glm::vec2 extent = half + 1.0f / pixels;  // room for one pixel of antialiasing
    const float border = std::clamp(shape.borderWidth, 0.0f, std::min(half.x, half.y));
    const glm::vec4 params(half, radius, border);
    const std::uint32_t color = packColor(shape.color);
    const std::uint32_t borderColor = packColor(shape.borderColor);

    const glm::vec2 offsets[4] = {{-extent.x, -extent.y}, {extent.x, -extent.y}, {extent.x, extent.y}, {-extent.x, extent.y}};
    Vertex corners[4];
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 p = rect.toScreen * glm::vec3(centre + offsets[i], 1.0f);
        corners[i] = {glm::vec2(p), offsets[i], color, borderColor, params, Shape};
    }
    const Vertex quad[6] = {corners[0], corners[1], corners[2], corners[0], corners[2], corners[3]};
    addTriangles(0, quad, 6);
}

void UIRenderer::imageQuad(GLuint texture, const UIRect& rect, glm::vec2 min, glm::vec2 max, glm::vec2 uvMin,
                           glm::vec2 uvMax, std::uint32_t color)
{
    if (max.x <= min.x || max.y <= min.y)
        return;
    const glm::vec2 local[4] = {min, {max.x, min.y}, max, {min.x, max.y}};
    const glm::vec2 uv[4] = {uvMin, {uvMax.x, uvMin.y}, uvMax, {uvMin.x, uvMax.y}};
    Vertex corners[4];
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 p = rect.toScreen * glm::vec3(local[i], 1.0f);
        corners[i] = {glm::vec2(p), uv[i], color, 0u, glm::vec4(0.0f), Image};
    }
    const Vertex quad[6] = {corners[0], corners[1], corners[2], corners[0], corners[2], corners[3]};
    addTriangles(texture, quad, 6);
}

// A point on the [-1, 1] square's edge, angle clockwise from straight up (y down)
static glm::vec2 squareEdge(float angle)
{
    const glm::vec2 d(std::sin(angle), -std::cos(angle));
    return d / std::max(std::abs(d.x), std::abs(d.y));
}

void UIRenderer::drawImage(const UIImage& image, const UIRect& rect)
{
    const GLuint texture = image.texture != InvalidTexture ? image.texture : m_white;
    const std::uint32_t color = packColor(image.color);
    glm::vec2 min = glm::min(rect.min, rect.max), max = glm::max(rect.min, rect.max);
    const glm::vec2 textureSize = image.texture != InvalidTexture ? image.textureSize : glm::vec2(0.0f);

    if (image.type == UIImageType::Sliced && textureSize.x > 0.0f && textureSize.y > 0.0f) {
        // left, top, right, bottom; shrunk evenly where the rect is too small for them
        glm::vec4 border = glm::max(image.border, glm::vec4(0.0f));
        const glm::vec2 size = max - min;
        const float sx = border.x + border.z > size.x ? size.x / (border.x + border.z) : 1.0f;
        const float sy = border.y + border.w > size.y ? size.y / (border.y + border.w) : 1.0f;
        const float xs[4] = {min.x, min.x + border.x * sx, max.x - border.z * sx, max.x};
        const float ys[4] = {min.y, min.y + border.y * sy, max.y - border.w * sy, max.y};
        const float us[4] = {0.0f, border.x / textureSize.x, 1.0f - border.z / textureSize.x, 1.0f};
        const float vs[4] = {0.0f, border.y / textureSize.y, 1.0f - border.w / textureSize.y, 1.0f};
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x)
                imageQuad(texture, rect, {xs[x], ys[y]}, {xs[x + 1], ys[y + 1]}, {us[x], vs[y]}, {us[x + 1], vs[y + 1]},
                          color);
        }
        return;
    }

    if (image.preserveAspect && textureSize.x > 0.0f && textureSize.y > 0.0f) {
        const glm::vec2 size = max - min;
        const float scale = std::min(size.x / textureSize.x, size.y / textureSize.y);
        const glm::vec2 fitted = textureSize * scale;
        min += (size - fitted) * 0.5f;
        max = min + fitted;
    }
    if (image.type != UIImageType::Filled) {
        imageQuad(texture, rect, min, max, glm::vec2(0.0f), glm::vec2(1.0f), color);
        return;
    }

    const float fill = std::clamp(image.fillAmount, 0.0f, 1.0f);
    if (fill <= 0.0f)
        return;
    const bool fromEnd = image.fillOrigin == UIFillOrigin::RightOrTop;
    switch (image.fillMethod) {
    case UIFillMethod::Horizontal: {
        const float x = fromEnd ? max.x - (max.x - min.x) * fill : min.x + (max.x - min.x) * fill;
        if (fromEnd)
            imageQuad(texture, rect, {x, min.y}, max, {1.0f - fill, 0.0f}, glm::vec2(1.0f), color);
        else
            imageQuad(texture, rect, min, {x, max.y}, glm::vec2(0.0f), {fill, 1.0f}, color);
        break;
    }
    case UIFillMethod::Vertical: {
        const float y = fromEnd ? min.y + (max.y - min.y) * fill : max.y - (max.y - min.y) * fill;
        if (fromEnd)
            imageQuad(texture, rect, min, {max.x, y}, glm::vec2(0.0f), {1.0f, fill}, color);
        else
            imageQuad(texture, rect, {min.x, y}, max, {0.0f, 1.0f - fill}, glm::vec2(1.0f), color);
        break;
    }
    case UIFillMethod::Radial360: {
        // A fan from the centre, broken at every corner and edge midpoint (multiples of 45 degrees)
        constexpr float pi = 3.14159265358979f;
        const float start = fromEnd ? 0.0f : pi;
        const float sweep = fill * 2.0f * pi;
        const glm::vec2 centre = (min + max) * 0.5f, half = (max - min) * 0.5f;
        const auto vertex = [&](glm::vec2 unit) {
            const glm::vec2 local = centre + unit * half;
            const glm::vec3 p = rect.toScreen * glm::vec3(local, 1.0f);
            return Vertex{glm::vec2(p), unit * 0.5f + 0.5f, color, 0u, glm::vec4(0.0f), Image};
        };
        Vertex tris[8 * 3];
        std::size_t count = 0;
        const Vertex mid = vertex(glm::vec2(0.0f));
        float from = 0.0f;
        while (from < sweep - 1e-5f && count < std::size(tris)) {
            const float to = std::min((std::floor(from / (pi * 0.25f) + 1e-4f) + 1.0f) * pi * 0.25f, sweep);
            tris[count++] = mid;
            tris[count++] = vertex(squareEdge(start + from));
            tris[count++] = vertex(squareEdge(start + to));
            from = to;
        }
        addTriangles(texture, tris, count);
        break;
    }
    }
}

void UIRenderer::end(const RenderTarget* target, int width, int height)
{
    m_stats = {m_vertices.size(), m_batches.size()};
    if (m_vertices.empty() || width <= 0 || height <= 0)
        return;
    if (target && !target->valid())
        return;

    if (target)
        target->bind();
    else
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const auto bytes = static_cast<GLsizeiptr>(m_vertices.size() * sizeof(Vertex));
    glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_STREAM_DRAW);  // orphan
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, m_vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_shader.use();
    glUniform2f(m_targetSizeLocation, static_cast<float>(width), static_cast<float>(height));
    glBindVertexArray(m_vao);
    glActiveTexture(GL_TEXTURE0);
    for (const Batch& batch : m_batches) {
        glBindTexture(GL_TEXTURE_2D, batch.texture ? batch.texture : m_white);
        glDrawArrays(GL_TRIANGLES, batch.first, batch.count);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void UIRenderer::pollShaderChanges()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastShaderPoll < std::chrono::milliseconds(500))
        return;
    m_lastShaderPoll = now;
    if (m_shader.reloadIfChanged())
        m_targetSizeLocation = m_shader.uniformLocation("uTargetSize");
}
