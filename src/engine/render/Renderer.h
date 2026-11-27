#pragma once

#include "engine/assets/Model.h"
#include "engine/gfx/DrawData.h"
#include "engine/gfx/GeometryBuffer.h"
#include "engine/gfx/IndirectBuffer.h"
#include "engine/gfx/PickBuffer.h"
#include "engine/gfx/RenderTarget.h"
#include "engine/gfx/Shader.h"
#include "engine/gfx/TextureArray.h"
#include "engine/scene/Camera.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using ModelId = std::uint32_t;
inline constexpr ModelId InvalidModel = UINT32_MAX;

// Every mesh lives in one GeometryBuffer and every texture in one TextureArray,
// so the whole scene is a single glMultiDrawElementsIndirect call with one
// instanced command per submesh.
class Renderer
{
public:
    // Starting capacities; all of them grow on demand except textureSize
    struct Config
    {
        GLsizeiptr vertices = 1 << 16;
        GLsizeiptr indices = 1 << 18;
        GLsizeiptr draws = 1024;
        GLsizei textureLayers = 8;
        GLsizei textureSize = 1024;
    };

    struct Stats
    {
        GLsizeiptr vertices, vertexCapacity;
        GLsizeiptr indices, indexCapacity;
        GLsizeiptr instances;
        GLsizeiptr draws, drawCapacity;
        GLsizei textureLayers, textureLayerCapacity, textureSize;
        std::size_t models;
    };

private:
    struct Submesh
    {
        MeshRange range;
        GLuint textureLayer;
    };

    GeometryBuffer m_geometry;
    IndirectBuffer m_indirect;
    TextureArray m_textures;
    Shader m_shader;
    Shader m_pickShader;
    Shader m_outlineShader;
    GLuint m_emptyVao = 0;  // the fullscreen triangle has no attributes, but core needs a VAO
    GLuint m_drawSsbo = 0;
    GLsizeiptr m_drawSsboCapacity = 0;

    struct ModelEntry
    {
        std::uint32_t firstSubmesh;
        std::uint32_t submeshCount;
    };

    std::vector<Submesh> m_submeshes;
    std::vector<ModelEntry> m_models;

    // Instances in submission order; uploadDraws groups them by submesh into m_draws
    std::vector<std::uint32_t> m_instanceSubmesh;
    std::vector<DrawData> m_instanceDraws;
    std::vector<DrawData> m_draws;
    std::vector<GLuint> m_groupStart;
    std::vector<GLuint> m_groupCursor;
    bool m_drawsDirty = false;
    bool m_mipsDirty = true;

    GLint m_viewProjLocation = -1;
    GLint m_pickViewProjLocation = -1;
    GLint m_outlineColorLocation = -1;
    GLint m_outlineWidthLocation = -1;
    std::chrono::steady_clock::time_point m_lastShaderPoll;
#ifdef NDEBUG
    bool m_shaderAutoReload = false;
#else
    bool m_shaderAutoReload = true;
#endif

    explicit Renderer(const Config& config);
    void onShadersLoaded();
    void uploadDraws();
    void prepare();
    void drawScene() const;

public:
    // Returns nullptr if the shaders fail to load
    static std::unique_ptr<Renderer> create();
    static std::unique_ptr<Renderer> create(const Config& config);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    ModelId addModel(const ModelData& model);
    void addInstance(ModelId model, const glm::mat4& transform, std::uint32_t entityId = PickBuffer::NoId,
                     bool highlight = false);
    void clearInstances();

    // target = nullptr draws to the window's framebuffer at width x height
    void render(const glm::mat4& viewProj, const RenderTarget* target, int width, int height);
    void render(const glm::mat4& viewProj, const RenderTarget& target)
    {
        render(viewProj, &target, target.width(), target.height());
    }

    // Entity ids and highlight flags for picking and outlines
    void renderIds(const glm::mat4& viewProj, const PickBuffer& target);
    // Draws over target around every highlighted pixel of ids (same size); color is linear
    void drawOutline(const RenderTarget& target, const PickBuffer& ids, const glm::vec4& color, int width);

    void reloadShaders();
    void pollShaderChanges();  // cheap to call every frame, checks file times twice a second
    void setShaderAutoReload(bool enabled) { m_shaderAutoReload = enabled; }
    bool shaderAutoReload() const          { return m_shaderAutoReload; }
    const std::string& shaderError() const;

    Stats stats() const;
};
