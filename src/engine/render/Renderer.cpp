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

    glEnable(GL_DEPTH_TEST);
}

std::unique_ptr<Renderer> Renderer::create()
{
    return create(Config{});
}

std::unique_ptr<Renderer> Renderer::create(const Config& config)
{
    std::unique_ptr<Renderer> renderer(new Renderer(config));
    if (!renderer->m_shader.load(resourcePath("shaders/mesh.vert"), resourcePath("shaders/mesh.frag")))
        return nullptr;
    renderer->onShaderLoaded();
    return renderer;
}

Renderer::~Renderer()
{
    glDeleteBuffers(1, &m_drawSsbo);
}

void Renderer::onShaderLoaded()
{
    m_shader.use();
    m_shader.setInt("uTextures", 0);
    m_viewProjLocation = m_shader.uniformLocation("uViewProj");
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

void Renderer::addInstance(ModelId model, const glm::mat4& transform)
{
    if (model >= m_models.size())
        return;
    const ModelEntry& entry = m_models[model];
    DrawData draw{};
    draw.setModel(transform);
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

void Renderer::setViewport(int width, int height)
{
    glViewport(0, 0, width, height);
}

void Renderer::render(const Camera& camera)
{
    if (m_mipsDirty) {
        m_textures.generateMipmaps();
        m_mipsDirty = false;
    }
    if (m_drawsDirty) {
        uploadDraws();
        m_drawsDirty = false;
    }

    // Only the scene is sRGB-encoded; UI drawn afterwards already has sRGB colors
    glEnable(GL_FRAMEBUFFER_SRGB);
    glClearColor(0.01f, 0.01f, 0.014f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_shader.use();
    Shader::setMat4(m_viewProjLocation, camera.getProjection() * camera.getView());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_drawSsbo);
    m_textures.bind(0);
    m_geometry.bind();
    m_indirect.draw();
    glDisable(GL_FRAMEBUFFER_SRGB);
}

void Renderer::reloadShaders()
{
    if (m_shader.reload())
        onShaderLoaded();
}

void Renderer::pollShaderChanges()
{
    if (!m_shaderAutoReload)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastShaderPoll < std::chrono::milliseconds(500))
        return;
    m_lastShaderPoll = now;
    if (m_shader.reloadIfChanged()) {
        onShaderLoaded();
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
