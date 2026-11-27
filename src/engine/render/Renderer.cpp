#include "Renderer.h"

#include "engine/core/Paths.h"

#include <algorithm>
#include <cstdio>

Renderer::Renderer(const Config& config)
    : m_geometry(config.vertices, config.indices, config.draws),
      m_indirect(config.draws),
      m_textures(config.textureSize, config.textureSize, config.textureLayers),
      m_drawSsboCapacity(std::max<GLsizeiptr>(config.draws, 1))
{
    glGenBuffers(1, &m_drawSsbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_drawSsbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_drawSsboCapacity * sizeof(DrawData), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glGenVertexArrays(1, &m_emptyVao);

    glEnable(GL_DEPTH_TEST);
}

std::unique_ptr<Renderer> Renderer::create()
{
    return create(Config{});
}

std::unique_ptr<Renderer> Renderer::create(const Config& config)
{
    std::unique_ptr<Renderer> renderer(new Renderer(config));
    const std::string meshVert = resourcePath("shaders/mesh.vert");
    if (!renderer->m_shader.load(meshVert, resourcePath("shaders/mesh.frag")) ||
        !renderer->m_pickShader.load(meshVert, resourcePath("shaders/pick.frag")) ||
        !renderer->m_outlineShader.load(resourcePath("shaders/fullscreen.vert"), resourcePath("shaders/outline.frag")))
        return nullptr;
    renderer->onShadersLoaded();
    return renderer;
}

Renderer::~Renderer()
{
    glDeleteBuffers(1, &m_drawSsbo);
    glDeleteVertexArrays(1, &m_emptyVao);
}

void Renderer::onShadersLoaded()
{
    m_shader.use();
    m_shader.setInt("uTextures", 0);
    m_viewProjLocation = m_shader.uniformLocation("uViewProj");
    m_pickViewProjLocation = m_pickShader.uniformLocation("uViewProj");
    m_outlineColorLocation = m_outlineShader.uniformLocation("uColor");
    m_outlineWidthLocation = m_outlineShader.uniformLocation("uWidth");
}

const std::string& Renderer::shaderError() const
{
    if (!m_shader.lastError().empty())
        return m_shader.lastError();
    if (!m_pickShader.lastError().empty())
        return m_pickShader.lastError();
    return m_outlineShader.lastError();
}

ModelId Renderer::addModel(const ModelData& model)
{
    std::vector<GLuint> layers;
    layers.reserve(model.textures.size());
    for (const ImageData& image : model.textures)
        layers.push_back(m_textures.addLayer(image.width, image.height, image.rgba.data()));
    if (!layers.empty())
        m_mipsDirty = true;

    const auto firstSubmesh = static_cast<std::uint32_t>(m_submeshes.size());
    for (const MeshData& mesh : model.meshes) {
        if (mesh.indices.empty())
            continue;
        const MeshRange range = m_geometry.addMesh(mesh.vertices, mesh.indices);
        const GLuint layer = mesh.texture >= 0 ? layers[static_cast<std::size_t>(mesh.texture)] : 0;
        m_submeshes.push_back({range, layer});
    }
    const auto submeshCount = static_cast<std::uint32_t>(m_submeshes.size()) - firstSubmesh;
    if (submeshCount == 0)
        return InvalidModel;

    m_models.push_back({firstSubmesh, submeshCount});
    return static_cast<ModelId>(m_models.size() - 1);
}

void Renderer::addInstance(ModelId model, const glm::mat4& transform, std::uint32_t entityId, bool highlight)
{
    if (model >= m_models.size())
        return;
    const ModelEntry& entry = m_models[model];
    DrawData draw{};
    draw.setModel(transform);
    draw.entityId = entityId;
    draw.highlight = highlight ? 1u : 0u;
    for (std::uint32_t i = 0; i < entry.submeshCount; ++i) {
        const std::uint32_t submesh = entry.firstSubmesh + i;
        draw.textureLayer = m_submeshes[submesh].textureLayer;
        m_instanceSubmesh.push_back(submesh);
        m_instanceDraws.push_back(draw);
    }
    m_drawsDirty = true;
}

void Renderer::clearInstances()
{
    m_instanceSubmesh.clear();
    m_instanceDraws.clear();
    m_drawsDirty = true;
}

// Counting sort by submesh, so each submesh's instances are contiguous and need one command
void Renderer::uploadDraws()
{
    const std::size_t submeshCount = m_submeshes.size();
    const std::size_t count = m_instanceDraws.size();

    m_groupStart.assign(submeshCount + 1, 0);
    for (const std::uint32_t submesh : m_instanceSubmesh)
        ++m_groupStart[submesh + 1];
    for (std::size_t i = 1; i <= submeshCount; ++i)
        m_groupStart[i] += m_groupStart[i - 1];

    m_groupCursor.assign(m_groupStart.begin(), m_groupStart.end() - 1);
    m_draws.resize(count);
    for (std::size_t i = 0; i < count; ++i)
        m_draws[m_groupCursor[m_instanceSubmesh[i]]++] = m_instanceDraws[i];

    m_indirect.clear();
    for (std::size_t s = 0; s < submeshCount; ++s) {
        const GLuint instances = m_groupStart[s + 1] - m_groupStart[s];
        if (instances > 0)
            m_indirect.add(m_submeshes[s].range, instances, m_groupStart[s]);
    }
    m_indirect.upload();

    const auto slots = static_cast<GLsizeiptr>(count);
    m_geometry.reserveDraws(slots);
    while (m_drawSsboCapacity < slots)
        m_drawSsboCapacity *= 2;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_drawSsbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_drawSsboCapacity * sizeof(DrawData), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, slots * sizeof(DrawData), m_draws.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void Renderer::prepare()
{
    if (m_mipsDirty) {
        m_textures.generateMipmaps();
        m_mipsDirty = false;
    }
    if (m_drawsDirty) {
        uploadDraws();
        m_drawsDirty = false;
    }
}

void Renderer::drawScene() const
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_drawSsbo);
    m_geometry.bind();
    m_indirect.draw();
}

void Renderer::render(const glm::mat4& viewProj, const RenderTarget* target, int width, int height)
{
    prepare();
    if (target)
        target->bind();
    else
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);

    // Only the scene is sRGB-encoded; UI drawn afterwards already has sRGB colors
    glEnable(GL_FRAMEBUFFER_SRGB);
    glClearColor(0.01f, 0.01f, 0.014f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_shader.use();
    Shader::setMat4(m_viewProjLocation, viewProj);
    m_textures.bind(0);
    drawScene();
    glDisable(GL_FRAMEBUFFER_SRGB);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::renderIds(const glm::mat4& viewProj, const PickBuffer& target)
{
    prepare();
    target.bind();
    glViewport(0, 0, target.width(), target.height());
    target.clear();
    m_pickShader.use();
    Shader::setMat4(m_pickViewProjLocation, viewProj);
    drawScene();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::drawOutline(const RenderTarget& target, const PickBuffer& ids, const glm::vec4& color, int width)
{
    target.bind();
    glViewport(0, 0, target.width(), target.height());
    glEnable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_DEPTH_TEST);

    m_outlineShader.use();
    glUniform4fv(m_outlineColorLocation, 1, &color.x);
    glUniform1i(m_outlineWidthLocation, width);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ids.idTexture());
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::reloadShaders()
{
    const bool mesh = m_shader.reload();
    const bool pick = m_pickShader.reload();
    const bool outline = m_outlineShader.reload();
    if (mesh || pick || outline)
        onShadersLoaded();
}

void Renderer::pollShaderChanges()
{
    if (!m_shaderAutoReload)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastShaderPoll < std::chrono::milliseconds(500))
        return;
    m_lastShaderPoll = now;
    const bool mesh = m_shader.reloadIfChanged();
    const bool pick = m_pickShader.reloadIfChanged();
    const bool outline = m_outlineShader.reloadIfChanged();
    if (mesh || pick || outline) {
        onShadersLoaded();
        std::printf("Shaders reloaded\n");
    }
}

Renderer::Stats Renderer::stats() const
{
    Stats s{};
    s.vertices = m_geometry.vertexCount();
    s.vertexCapacity = m_geometry.vertexCapacity();
    s.indices = m_geometry.indexCount();
    s.indexCapacity = m_geometry.indexCapacity();
    s.instances = static_cast<GLsizeiptr>(m_draws.size());
    s.draws = m_indirect.size();
    s.drawCapacity = m_indirect.capacity();
    s.textureLayers = m_textures.layerCount();
    s.textureLayerCapacity = m_textures.layerCapacity();
    s.textureSize = m_textures.layerSize();
    s.models = m_models.size();
    return s;
}
