#include "Renderer.h"

#include "core/Paths.h"

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
    m_textures.generateMipmaps();
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
}

ModelId Renderer::addModel(const ModelData& model)
{
    std::vector<GLuint> layers;
    layers.reserve(model.textures.size());
    for (const ImageData& image : model.textures)
        layers.push_back(m_textures.addLayer(image.width, image.height, image.rgba.data()));
    if (!layers.empty())
        m_textures.generateMipmaps();

    std::vector<Submesh> submeshes;
    for (const MeshData& mesh : model.meshes) {
        if (mesh.indices.empty())
            continue;
        const MeshRange range = m_geometry.addMesh(mesh.vertices, mesh.indices);
        const GLuint layer = mesh.texture >= 0 ? layers[static_cast<std::size_t>(mesh.texture)] : 0;
        submeshes.push_back({range, layer});
    }
    if (submeshes.empty())
        return InvalidModel;

    m_models.push_back(std::move(submeshes));
    return static_cast<ModelId>(m_models.size() - 1);
}

void Renderer::addInstance(ModelId model, const glm::mat4& transform)
{
    if (model >= m_models.size())
        return;
    for (const Submesh& submesh : m_models[model]) {
        DrawData draw{};
        draw.model = transform;
        draw.textureLayer = submesh.textureLayer;
        m_draws.push_back(draw);
        m_drawRanges.push_back(submesh.range);
    }
    m_drawsDirty = true;
}

void Renderer::clearInstances()
{
    m_draws.clear();
    m_drawRanges.clear();
    m_drawsDirty = true;
}

void Renderer::uploadDraws()
{
    const auto count = static_cast<GLsizeiptr>(m_draws.size());
    m_geometry.reserveDraws(count);

    m_indirect.clear();
    for (const MeshRange& range : m_drawRanges)
        m_indirect.add(range);
    m_indirect.upload();

    while (m_drawSsboCapacity < count)
        m_drawSsboCapacity *= 2;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_drawSsbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_drawSsboCapacity * sizeof(DrawData), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, count * sizeof(DrawData), m_draws.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void Renderer::setViewport(int width, int height)
{
    glViewport(0, 0, width, height);
}

void Renderer::render(const Camera& camera)
{
    if (m_drawsDirty) {
        uploadDraws();
        m_drawsDirty = false;
    }

    // Only the scene is sRGB-encoded; UI drawn afterwards already has sRGB colors
    glEnable(GL_FRAMEBUFFER_SRGB);
    glClearColor(0.01f, 0.01f, 0.014f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_shader.use();
    m_shader.setMat4("uViewProj", camera.getProjection() * camera.getView());
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
    s.draws = m_indirect.size();
    s.drawCapacity = m_indirect.capacity();
    s.textureLayers = m_textures.layerCount();
    s.textureLayerCapacity = m_textures.layerCapacity();
    s.textureSize = m_textures.layerSize();
    s.models = m_models.size();
    return s;
}
