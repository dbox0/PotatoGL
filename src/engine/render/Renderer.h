#pragma once

#include "engine/assets/Model.h"
#include "engine/gfx/DrawData.h"
#include "engine/gfx/GeometryBuffer.h"
#include "engine/gfx/GpuProfiler.h"
#include "engine/gfx/IndirectBuffer.h"
#include "engine/gfx/LightData.h"
#include "engine/gfx/OutlineMask.h"
#include "engine/gfx/PickBuffer.h"
#include "engine/gfx/RenderTarget.h"
#include "engine/gfx/Shader.h"
#include "engine/gfx/ShadowMap.h"
#include "engine/gfx/TextureArray.h"
#include "engine/render/LightClusters.h"
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
        std::size_t lights;
        std::size_t clusterRefs;  // light references across all clusters, last view rendered
    };

    // Directional light only. The map covers a sphere around the first `distance` metres of the view.
    struct ShadowSettings
    {
        bool enabled = true;
        int size = 2048;
        float distance = 30.0f;
    };

private:
    struct Submesh
    {
        MeshRange range;
        GLuint textureLayer;
    };

    GpuProfiler m_profiler;
    GeometryBuffer m_geometry;
    IndirectBuffer m_indirect;
    IndirectBuffer m_highlightIndirect;  // highlighted instances only; they come first in each group
    TextureArray m_textures;
    Shader m_shader;
    Shader m_pickShader;
    Shader m_maskShader;
    Shader m_dilateShader;
    Shader m_outlineShader;
    Shader m_tonemapShader;
    Shader m_depthShader;
    RenderTarget m_windowTarget;  // HDR scene for render(target = nullptr)
    ShadowMap m_shadowMap;
    LightClusters m_clusters;
    GLuint m_emptyVao = 0;  // the fullscreen triangle has no attributes, but core needs a VAO
    GLuint m_drawSsbo = 0;
    GLsizeiptr m_drawSsboCapacity = 0;
    GLuint m_lightSsbo = 0;
    GLuint m_clusterSsbo = 0;
    GLuint m_clusterLightSsbo = 0;
    GLsizeiptr m_lightSsboBytes = 64 * sizeof(LightData);
    GLsizeiptr m_clusterSsboBytes = LightClusters::Count * sizeof(LightClusters::Range);
    GLsizeiptr m_clusterLightSsboBytes = 4096 * sizeof(std::uint32_t);

    std::vector<LightData> m_lights;
    glm::vec3 m_sunDirection{0.0f, 1.0f, 0.0f};
    glm::vec3 m_sunRadiance{0.0f};
    glm::vec3 m_skyColor{0.30f, 0.36f, 0.45f};
    glm::vec3 m_groundColor{0.10f, 0.09f, 0.08f};
    float m_exposure = 1.0f;
    bool m_lightsDirty = true;
    bool m_depthPrepass = false;
    ShadowSettings m_shadowSettings;
    glm::mat4 m_shadowMatrix{1.0f};
    float m_shadowNormalOffset = 0.0f;

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
    std::vector<GLuint> m_highlightCursor;
    bool m_drawsDirty = false;
    bool m_mipsDirty = true;

    GLint m_viewProjLocation = -1;
    GLint m_eyeLocation = -1;
    GLint m_viewDepthLocation = -1;
    GLint m_clusterTileLocation = -1;
    GLint m_clusterZLocation = -1;
    GLint m_shadowsLocation = -1;
    GLint m_shadowMatrixLocation = -1;
    GLint m_shadowNormalOffsetLocation = -1;
    GLint m_shadowDistanceLocation = -1;
    GLint m_depthViewProjLocation = -1;
    GLint m_sunDirectionLocation = -1;
    GLint m_sunRadianceLocation = -1;
    GLint m_skyColorLocation = -1;
    GLint m_groundColorLocation = -1;
    GLint m_exposureLocation = -1;
    GLint m_pickViewProjLocation = -1;
    GLint m_maskViewProjLocation = -1;
    GLint m_dilateWidthLocation = -1;
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
    void uploadLights();
    void uploadClusters();
    void renderShadowMap(const ViewMatrices& view);
    void prepare();
    void drawScene(const IndirectBuffer& draws) const;

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

    // Clears the directional light too
    void clearLights();
    // direction points from surfaces towards the light; only one directional light
    void setDirectionalLight(const glm::vec3& direction, const glm::vec3& color, float intensity);
    void addPointLight(const glm::vec3& position, const glm::vec3& color, float intensity, float range);
    // direction is where the light points; angles are half-angles from the cone axis
    void addSpotLight(const glm::vec3& position, const glm::vec3& direction, const glm::vec3& color,
                      float intensity, float range, float innerDeg, float outerDeg);
    // Hemisphere ambient, blended by the normal's y
    void setAmbient(const glm::vec3& sky, const glm::vec3& ground)
    {
        m_skyColor = sky;
        m_groundColor = ground;
    }
    void setExposure(float exposure) { m_exposure = exposure; }
    float exposure() const           { return m_exposure; }

    // Depth-only pass first, so the lit pass shades each pixel once
    void setDepthPrepass(bool enabled) { m_depthPrepass = enabled; }
    bool depthPrepass() const          { return m_depthPrepass; }
    ShadowSettings& shadowSettings()   { return m_shadowSettings; }

    // target = nullptr draws to the window's framebuffer at width x height
    void render(const ViewMatrices& view, const RenderTarget* target, int width, int height);
    void render(const ViewMatrices& view, const RenderTarget& target)
    {
        render(view, &target, target.width(), target.height());
    }

    // Entity ids and highlight flags for picking
    void renderIds(const glm::mat4& viewProj, const PickBuffer& target);
    // Visible silhouette of the highlighted instances; target must already hold this frame's depth
    void renderMask(const glm::mat4& viewProj, const RenderTarget& target, OutlineMask& mask);
    // Draws over target within width pixels outside the mask from renderMask; color is linear
    void drawOutline(const RenderTarget& target, const OutlineMask& mask, const glm::vec4& color, int width);
    bool hasHighlight() const { return m_highlightIndirect.size() > 0; }

    void reloadShaders();
    void pollShaderChanges();  // cheap to call every frame, checks file times twice a second
    void setShaderAutoReload(bool enabled) { m_shaderAutoReload = enabled; }
    bool shaderAutoReload() const          { return m_shaderAutoReload; }
    const std::string& shaderError() const;

    Stats stats() const;
    GpuProfiler& profiler() { return m_profiler; }
};
