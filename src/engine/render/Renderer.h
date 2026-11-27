#pragma once

#include "engine/assets/Model.h"
#include "engine/gfx/DrawData.h"
#include "engine/gfx/GeometryBuffer.h"
#include "engine/gfx/GpuProfiler.h"
#include "engine/gfx/IndirectBuffer.h"
#include "engine/gfx/LightData.h"
#include "engine/gfx/MaterialData.h"
#include "engine/gfx/OutlineMask.h"
#include "engine/gfx/PickBuffer.h"
#include "engine/gfx/RenderTarget.h"
#include "engine/gfx/Shader.h"
#include "engine/gfx/ShadowMap.h"
#include "engine/gfx/SkinnedGeometry.h"
#include "engine/gfx/TextureArray.h"
#include "engine/render/LightClusters.h"
#include "engine/scene/Camera.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

using ModelId = std::uint32_t;
inline constexpr ModelId InvalidModel = UINT32_MAX;
// One skinned instance's joint palette and skinned vertices. The low 20 bits index the slot,
// the rest is its generation, so a stale id never reaches a slot that was reused.
using SkinId = std::uint32_t;
inline constexpr SkinId InvalidSkin = UINT32_MAX;

// Every mesh lives in one GeometryBuffer and every texture in one TextureArray,
// so the whole scene is a single glMultiDrawElementsIndirect call with one
// instanced command per submesh. Skinned meshes are a second one: beginFrame() skins them
// all in one compute dispatch, and every pass draws the result.
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
        std::size_t materials;
        std::size_t lights;
        std::size_t clusterRefs;  // light references across all clusters, last view rendered
        std::size_t skins;
        GLsizeiptr skinnedVertices;    // output vertices allocated to skins
        GLsizeiptr skinnedThisFrame;   // vertices the last beginFrame skinned
    };

    // Directional light only. The map covers a sphere around the first `distance` metres of the view.
    struct Bounds
    {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    // Always exists: white, untextured; used by meshes imported without a material
    static constexpr MaterialId DefaultMaterial = 0;

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
        MaterialId material;
        bool bindPose = false;  // static copy of a skinned submesh, drawn by plain instances only
    };

    struct SkinnedSubmesh
    {
        GLuint firstIndex;
        GLuint indexCount;
        GLuint vertexOffset;  // into the model's skinned vertices
        MaterialId material;
    };

    struct Skin
    {
        ModelId model = InvalidModel;  // InvalidModel: free slot
        GLuint generation = 0;
        GLuint output = 0;     // first vertex of its output region
        GLuint jointBase = 0;  // first joint of its palette slice
        bool used = false;     // acquired since the last beginFrame
        bool dirty = false;    // palette written, needs skinning
    };

    GpuProfiler m_profiler;
    GeometryBuffer m_geometry;
    SkinnedGeometry m_skinned;
    IndirectBuffer m_indirect;
    IndirectBuffer m_highlightIndirect;  // highlighted instances only; they come first in each group
    IndirectBuffer m_skinnedIndirect;    // one command per skinned instance and submesh
    IndirectBuffer m_skinnedHighlightIndirect;
    TextureArray m_textures;
    Shader m_shader;
    Shader m_pickShader;
    Shader m_maskShader;
    Shader m_dilateShader;
    Shader m_outlineShader;
    Shader m_tonemapShader;
    Shader m_depthShader;
    Shader m_skinShader;
    RenderTarget m_windowTarget;  // HDR scene for render(target = nullptr)
    ShadowMap m_shadowMap;
    LightClusters m_clusters;
    GLuint m_emptyVao = 0;  // the fullscreen triangle has no attributes, but core needs a VAO
    GLuint m_drawSsbo = 0;
    GLsizeiptr m_drawSsboCapacity = 0;
    GLuint m_lightSsbo = 0;
    GLuint m_clusterSsbo = 0;
    GLuint m_clusterLightSsbo = 0;
    GLuint m_materialSsbo = 0;
    GLsizeiptr m_materialSsboBytes = 64 * sizeof(GpuMaterial);
    GLsizeiptr m_lightSsboBytes = 64 * sizeof(LightData);
    GLsizeiptr m_clusterSsboBytes = LightClusters::Count * sizeof(LightClusters::Range);
    GLsizeiptr m_clusterLightSsboBytes = 4096 * sizeof(std::uint32_t);

    std::vector<LightData> m_lights;
    std::vector<GpuMaterial> m_materials;
    bool m_materialsDirty = true;
    glm::vec3 m_clearColor{0.01f, 0.01f, 0.014f};
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
        Bounds bounds;
        std::uint32_t firstSkinned = 0;
        std::uint32_t skinnedCount = 0;
        GLuint sourceFirst = 0;      // first source vertex in SkinnedGeometry
        GLuint skinnedVertices = 0;
        std::uint32_t jointCount = 0;
        std::vector<glm::vec4> bindPalette;  // three rows per joint
    };

    std::vector<Submesh> m_submeshes;
    std::vector<SkinnedSubmesh> m_skinnedSubmeshes;
    std::vector<ModelEntry> m_models;

    std::vector<Skin> m_skins;
    std::vector<std::uint32_t> m_freeSkins;
    RangeAllocator m_paletteJoints;
    std::vector<glm::vec4> m_palette;  // CPU staging; animators write their slice in place
    std::vector<SkinnedGeometry::Job> m_skinJobs;
    GLsizeiptr m_skinnedVerticesLastFrame = 0;

    // Instances in submission order; uploadDraws groups them by submesh into m_draws
    std::vector<std::uint32_t> m_instanceSubmesh;
    std::vector<DrawData> m_instanceDraws;
    std::vector<DrawData> m_draws;
    std::vector<GLuint> m_groupStart;
    std::vector<GLuint> m_groupCursor;
    std::vector<GLuint> m_highlightCursor;
    // Skinned instances, one entry per submesh; their slots follow the static ones
    std::vector<MeshRange> m_skinnedRanges;
    std::vector<DrawData> m_skinnedDraws;
    bool m_drawsDirty = false;
    // Swapped in by renderPreview so it doesn't disturb the scene's instances and lights
    std::vector<std::uint32_t> m_previewSubmesh;
    std::vector<DrawData> m_previewDraws;
    std::vector<MeshRange> m_previewSkinnedRanges;
    std::vector<DrawData> m_previewSkinnedDraws;
    std::vector<LightData> m_previewLights;
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
    void drawScene(bool highlightedOnly) const;
    Skin* skinSlot(SkinId id);
    void releaseSkin(std::uint32_t index);

public:
    // Returns nullptr if the shaders fail to load
    static std::unique_ptr<Renderer> create();
    static std::unique_ptr<Renderer> create(const Config& config);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Also adds the model's own textures and materials
    ModelId addModel(const ModelData& model);
    // Model space; zero-sized for an unknown model. Skinned models: bind pose.
    Bounds modelBounds(ModelId model) const;
    bool modelSkinned(ModelId model) const { return model < m_models.size() && m_models[model].skinnedCount > 0; }

    // sRGB RGBA8, scaled to the texture array's layer size. Returns the layer.
    GLuint addTexture(const ImageData& image);
    MaterialId addMaterial(const Material& material);
    void setMaterial(MaterialId id, const Material& material);

    // material overrides every submesh's own material; InvalidMaterial keeps them
    void addInstance(ModelId model, const glm::mat4& transform, std::uint32_t entityId = PickBuffer::NoId,
                     bool highlight = false, MaterialId material = InvalidMaterial);
    void clearInstances();

    // Skinned instances. acquireSkin keeps id if it is a live skin of model, else makes a new
    // one in bind pose; true if it made one. A skin nobody acquired since the last
    // beginFrame() is freed there, so acquire every frame while the instance exists.
    bool acquireSkin(SkinId& id, ModelId model);
    // The skin's palette slice to fill in place (model space, three rows per joint, joint
    // order of the skeleton); marks it for skinning this frame. Empty for a dead id.
    std::span<glm::vec4> writeSkinPalette(SkinId id);
    // Draws the model's skinned submeshes as the skin left them, its static ones as usual
    void addSkinnedInstance(SkinId skin, const glm::mat4& transform, std::uint32_t entityId = PickBuffer::NoId,
                            bool highlight = false, MaterialId material = InvalidMaterial);
    // Once per frame, after instances are submitted and before any view renders:
    // frees unused skins, uploads the palette and skins every changed instance
    void beginFrame();

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

    // One model, framed to fill target, under fixed studio lighting (asset thumbnails).
    // Leaves the scene's instances and lights as they were. from: direction towards the camera.
    void renderPreview(ModelId model, MaterialId material, const RenderTarget& target, const glm::vec3& from);

    // Entity ids and highlight flags for picking
    void renderIds(const glm::mat4& viewProj, const PickBuffer& target);
    // Visible silhouette of the highlighted instances; target must already hold this frame's depth
    void renderMask(const glm::mat4& viewProj, const RenderTarget& target, OutlineMask& mask);
    // Draws over target within width pixels outside the mask from renderMask; color is linear
    void drawOutline(const RenderTarget& target, const OutlineMask& mask, const glm::vec4& color, int width);
    bool hasHighlight() const { return m_highlightIndirect.size() > 0 || m_skinnedHighlightIndirect.size() > 0; }

    void reloadShaders();
    void pollShaderChanges();  // cheap to call every frame, checks file times twice a second
    void setShaderAutoReload(bool enabled) { m_shaderAutoReload = enabled; }
    bool shaderAutoReload() const          { return m_shaderAutoReload; }
    const std::string& shaderError() const;

    Stats stats() const;
    GpuProfiler& profiler() { return m_profiler; }
};
