#pragma once

#include "assets/Model.h"
#include "gfx/DrawData.h"
#include "gfx/GeometryBuffer.h"
#include "gfx/IndirectBuffer.h"
#include "gfx/Shader.h"
#include "gfx/TextureArray.h"
#include "scene/Camera.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using ModelId = std::uint32_t;
inline constexpr ModelId InvalidModel = UINT32_MAX;

// Every mesh lives in one GeometryBuffer and every texture in one TextureArray,
// so the whole scene is a single glMultiDrawElementsIndirect call.
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
    GLuint m_drawSsbo = 0;
    GLsizeiptr m_drawSsboCapacity = 0;

    std::vector<std::vector<Submesh>> m_models;
    std::vector<DrawData> m_draws;
    std::vector<MeshRange> m_drawRanges;
    bool m_drawsDirty = false;

    std::chrono::steady_clock::time_point m_lastShaderPoll;

    explicit Renderer(const Config& config);
    void onShaderLoaded();
    void uploadDraws();

public:
    // Returns nullptr if the shaders fail to load
    static std::unique_ptr<Renderer> create();
    static std::unique_ptr<Renderer> create(const Config& config);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    ModelId addModel(const ModelData& model);
    void addInstance(ModelId model, const glm::mat4& transform);
    void clearInstances();

    void setViewport(int width, int height);
    void render(const Camera& camera);

    void reloadShaders();
    void pollShaderChanges();  // cheap to call every frame, checks file times twice a second
    const std::string& shaderError() const { return m_shader.lastError(); }

    Stats stats() const;
};
