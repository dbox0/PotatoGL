#include "Renderer.h"

#include "engine/core/Paths.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

// Orphans the buffer and refills it, doubling its capacity until the data fits
static void streamBuffer(GLuint buffer, GLsizeiptr& capacity, const void* data, GLsizeiptr bytes)
{
    while (capacity < bytes)
        capacity *= 2;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, capacity, nullptr, GL_STREAM_DRAW);
    if (bytes > 0)
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bytes, data);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

Renderer::Renderer(const Config& config)
    : m_geometry(config.vertices, config.indices, config.draws),
      m_indirect(config.draws),
      m_highlightIndirect(config.draws),
      m_textures(config.textureSize, config.textureSize, config.textureLayers),
      m_drawSsboCapacity(std::max<GLsizeiptr>(config.draws, 1))
{
    glGenBuffers(1, &m_drawSsbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_drawSsbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_drawSsboCapacity * sizeof(DrawData), nullptr, GL_DYNAMIC_DRAW);
    glGenBuffers(1, &m_lightSsbo);
    glGenBuffers(1, &m_clusterSsbo);
    glGenBuffers(1, &m_clusterLightSsbo);
    glGenBuffers(1, &m_materialSsbo);
    streamBuffer(m_lightSsbo, m_lightSsboBytes, nullptr, 0);
    streamBuffer(m_clusterSsbo, m_clusterSsboBytes, nullptr, 0);
    streamBuffer(m_clusterLightSsbo, m_clusterLightSsboBytes, nullptr, 0);
    addMaterial(Material{});  // DefaultMaterial
    glGenVertexArrays(1, &m_emptyVao);
    // Allocated up front so the shadow sampler always has a depth texture behind it
    m_shadowMap.resize(m_shadowSettings.size);

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
        !renderer->m_maskShader.load(meshVert, resourcePath("shaders/mask.frag")) ||
        !renderer->m_dilateShader.load(resourcePath("shaders/fullscreen.vert"), resourcePath("shaders/dilate.frag")) ||
        !renderer->m_outlineShader.load(resourcePath("shaders/fullscreen.vert"), resourcePath("shaders/outline.frag")) ||
        !renderer->m_tonemapShader.load(resourcePath("shaders/fullscreen.vert"), resourcePath("shaders/tonemap.frag")) ||
        !renderer->m_depthShader.load(meshVert, resourcePath("shaders/depth.frag")))
        return nullptr;
    renderer->onShadersLoaded();
    return renderer;
}

Renderer::~Renderer()
{
    glDeleteBuffers(1, &m_drawSsbo);
    glDeleteBuffers(1, &m_lightSsbo);
    glDeleteBuffers(1, &m_clusterSsbo);
    glDeleteBuffers(1, &m_clusterLightSsbo);
    glDeleteBuffers(1, &m_materialSsbo);
    glDeleteVertexArrays(1, &m_emptyVao);
}

void Renderer::onShadersLoaded()
{
    m_shader.use();
    m_shader.setInt("uTextures", 0);
    m_viewProjLocation = m_shader.uniformLocation("uViewProj");
    m_eyeLocation = m_shader.uniformLocation("uEye");
    m_viewDepthLocation = m_shader.uniformLocation("uViewDepth");
    m_clusterTileLocation = m_shader.uniformLocation("uClusterTile");
    m_clusterZLocation = m_shader.uniformLocation("uClusterZ");
    m_shadowsLocation = m_shader.uniformLocation("uShadows");
    m_shadowMatrixLocation = m_shader.uniformLocation("uShadowMatrix");
    m_shadowNormalOffsetLocation = m_shader.uniformLocation("uShadowNormalOffset");
    m_shadowDistanceLocation = m_shader.uniformLocation("uShadowDistance");
    m_depthViewProjLocation = m_depthShader.uniformLocation("uViewProj");
    m_sunDirectionLocation = m_shader.uniformLocation("uSunDirection");
    m_sunRadianceLocation = m_shader.uniformLocation("uSunRadiance");
    m_skyColorLocation = m_shader.uniformLocation("uSkyColor");
    m_groundColorLocation = m_shader.uniformLocation("uGroundColor");
    m_exposureLocation = m_tonemapShader.uniformLocation("uExposure");
    m_pickViewProjLocation = m_pickShader.uniformLocation("uViewProj");
    m_maskViewProjLocation = m_maskShader.uniformLocation("uViewProj");
    m_dilateWidthLocation = m_dilateShader.uniformLocation("uWidth");
    m_outlineColorLocation = m_outlineShader.uniformLocation("uColor");
    m_outlineWidthLocation = m_outlineShader.uniformLocation("uWidth");
}

const std::string& Renderer::shaderError() const
{
    if (!m_shader.lastError().empty())
        return m_shader.lastError();
    if (!m_pickShader.lastError().empty())
        return m_pickShader.lastError();
    if (!m_maskShader.lastError().empty())
        return m_maskShader.lastError();
    if (!m_dilateShader.lastError().empty())
        return m_dilateShader.lastError();
    if (!m_outlineShader.lastError().empty())
        return m_outlineShader.lastError();
    if (!m_tonemapShader.lastError().empty())
        return m_tonemapShader.lastError();
    return m_depthShader.lastError();
}

ModelId Renderer::addModel(const ModelData& model)
{
    std::vector<GLuint> layers;
    layers.reserve(model.textures.size());
    for (const ImageData& image : model.textures)
        layers.push_back(addTexture(image));

    std::vector<MaterialId> materials;
    materials.reserve(model.materials.size());
    for (const MaterialData& data : model.materials) {
        Material material;
        material.baseColor = data.baseColor;
        material.emission = data.emission;
        material.metallic = data.metallic;
        material.smoothness = data.smoothness;
        if (data.texture >= 0 && static_cast<std::size_t>(data.texture) < layers.size())
            material.textureLayer = layers[static_cast<std::size_t>(data.texture)];
        materials.push_back(addMaterial(material));
    }

    Bounds bounds{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
    const auto firstSubmesh = static_cast<std::uint32_t>(m_submeshes.size());
    for (const MeshData& mesh : model.meshes) {
        if (mesh.indices.empty())
            continue;
        for (const Vertex& v : mesh.vertices) {
            bounds.min = glm::min(bounds.min, v.position);
            bounds.max = glm::max(bounds.max, v.position);
        }
        const MeshRange range = m_geometry.addMesh(mesh.vertices, mesh.indices);
        const bool hasMaterial = mesh.material >= 0 && static_cast<std::size_t>(mesh.material) < materials.size();
        m_submeshes.push_back({range, hasMaterial ? materials[static_cast<std::size_t>(mesh.material)] : DefaultMaterial});
    }
    const auto submeshCount = static_cast<std::uint32_t>(m_submeshes.size()) - firstSubmesh;
    if (submeshCount == 0)
        return InvalidModel;

    m_models.push_back({firstSubmesh, submeshCount, bounds});
    return static_cast<ModelId>(m_models.size() - 1);
}

Renderer::Bounds Renderer::modelBounds(ModelId model) const
{
    return model < m_models.size() ? m_models[model].bounds : Bounds{};
}

GLuint Renderer::addTexture(const ImageData& image)
{
    m_mipsDirty = true;
    return m_textures.addLayer(image.width, image.height, image.rgba.data());
}

MaterialId Renderer::addMaterial(const Material& material)
{
    m_materials.push_back(GpuMaterial::from(material));
    m_materialsDirty = true;
    return static_cast<MaterialId>(m_materials.size() - 1);
}

void Renderer::setMaterial(MaterialId id, const Material& material)
{
    if (id >= m_materials.size())
        return;
    m_materials[id] = GpuMaterial::from(material);
    m_materialsDirty = true;
}

void Renderer::addInstance(ModelId model, const glm::mat4& transform, std::uint32_t entityId, bool highlight,
                           MaterialId material)
{
    if (model >= m_models.size())
        return;
    const ModelEntry& entry = m_models[model];
    const bool overridden = material < m_materials.size();
    DrawData draw{};
    draw.setModel(transform);
    draw.entityId = entityId;
    draw.highlight = highlight ? 1u : 0u;
    for (std::uint32_t i = 0; i < entry.submeshCount; ++i) {
        const std::uint32_t submesh = entry.firstSubmesh + i;
        draw.material = overridden ? material : m_submeshes[submesh].material;
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

void Renderer::clearLights()
{
    m_lights.clear();
    m_sunRadiance = glm::vec3(0.0f);
    m_lightsDirty = true;
}

void Renderer::setDirectionalLight(const glm::vec3& direction, const glm::vec3& color, float intensity)
{
    m_sunDirection = glm::normalize(direction);
    m_sunRadiance = color * intensity;
}

void Renderer::addPointLight(const glm::vec3& position, const glm::vec3& color, float intensity, float range)
{
    const float r = std::max(range, 1e-3f);
    m_lights.push_back({glm::vec4(position, 1.0f / (r * r)), glm::vec4(color * intensity, 0.0f),
                        glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)});
    m_lightsDirty = true;
}

// Cone factor = saturate(cos(angle) * scale + offset): 0 at the outer angle, 1 inside the inner one
void Renderer::addSpotLight(const glm::vec3& position, const glm::vec3& direction, const glm::vec3& color,
                            float intensity, float range, float innerDeg, float outerDeg)
{
    const float r = std::max(range, 1e-3f);
    const float cosOuter = std::cos(glm::radians(std::clamp(outerDeg, 0.0f, 89.9f)));
    const float cosInner = std::cos(glm::radians(std::clamp(innerDeg, 0.0f, outerDeg)));
    const float scale = 1.0f / std::max(cosInner - cosOuter, 1e-4f);
    const float offset = -cosOuter * scale;
    m_lights.push_back({glm::vec4(position, 1.0f / (r * r)), glm::vec4(color * intensity, scale),
                        glm::vec4(glm::normalize(direction), offset)});
    m_lightsDirty = true;
}

void Renderer::uploadLights()
{
    streamBuffer(m_lightSsbo, m_lightSsboBytes, m_lights.data(),
                 static_cast<GLsizeiptr>(m_lights.size() * sizeof(LightData)));
}

void Renderer::uploadClusters()
{
    const auto& ranges = m_clusters.ranges();
    const auto& indices = m_clusters.indices();
    streamBuffer(m_clusterSsbo, m_clusterSsboBytes, ranges.data(),
                 static_cast<GLsizeiptr>(ranges.size() * sizeof(LightClusters::Range)));
    streamBuffer(m_clusterLightSsbo, m_clusterLightSsboBytes, indices.data(),
                 static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)));
}

// Fits an orthographic box to the bounding sphere of the view frustum's first `distance` metres.
// The sphere's radius doesn't change as the camera turns, and snapping its center to whole texels
// keeps shadow edges from shimmering as the camera moves.
void Renderer::renderShadowMap(const ViewMatrices& view)
{
    const ShadowSettings& settings = m_shadowSettings;
    m_shadowMap.resize(settings.size);

    float zNear, zFar;
    clipPlanes(view.proj, zNear, zFar);
    const float t = std::clamp((settings.distance - zNear) / (zFar - zNear), 0.0f, 1.0f);
    const glm::mat4 invViewProj = glm::inverse(view.viewProj());
    glm::vec3 corners[8];
    glm::vec3 center(0.0f);
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 xy(i & 1 ? 1.0f : -1.0f, i & 2 ? 1.0f : -1.0f);
        const glm::vec4 n = invViewProj * glm::vec4(xy, -1.0f, 1.0f);
        const glm::vec4 f = invViewProj * glm::vec4(xy, 1.0f, 1.0f);
        corners[i] = glm::vec3(n) / n.w;
        corners[i + 4] = glm::mix(corners[i], glm::vec3(f) / f.w, t);
        center += corners[i] + corners[i + 4];
    }
    center /= 8.0f;
    float radius = 0.0f;
    for (const glm::vec3& corner : corners)
        radius = std::max(radius, glm::length(corner - center));
    radius = std::ceil(radius * 16.0f) / 16.0f;  // float noise must not change the texel size

    const glm::vec3 up = std::abs(m_sunDirection.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), -m_sunDirection, up);
    glm::vec3 c = glm::vec3(lightView * glm::vec4(center, 1.0f));
    const float texel = 2.0f * radius / static_cast<float>(settings.size);
    c.x = std::floor(c.x / texel) * texel;
    c.y = std::floor(c.y / texel) * texel;
    // Casters up to a radius beyond the sphere keep depth precision; depth clamp flattens farther ones
    // onto the near plane, where they still cast
    const glm::mat4 lightProj = glm::ortho(c.x - radius, c.x + radius, c.y - radius, c.y + radius,
                                           -(c.z + 2.0f * radius), -(c.z - radius));
    const glm::mat4 lightViewProj = lightProj * lightView;
    const glm::mat4 toUv = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
    m_shadowMatrix = toUv * lightViewProj;
    m_shadowNormalOffset = 1.5f * texel;

    m_shadowMap.bind();
    glViewport(0, 0, settings.size, settings.size);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_CLAMP);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    m_depthShader.use();
    Shader::setMat4(m_depthViewProjLocation, lightViewProj);
    drawScene(m_indirect);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DEPTH_CLAMP);
}

// Counting sort by submesh, so each submesh's instances are contiguous and need one command.
// Highlighted instances go first in their group, so the mask pass draws just them.
void Renderer::uploadDraws()
{
    const std::size_t submeshCount = m_submeshes.size();
    const std::size_t count = m_instanceDraws.size();

    m_groupStart.assign(submeshCount + 1, 0);
    for (const std::uint32_t submesh : m_instanceSubmesh)
        ++m_groupStart[submesh + 1];
    for (std::size_t i = 1; i <= submeshCount; ++i)
        m_groupStart[i] += m_groupStart[i - 1];

    m_highlightCursor.assign(m_groupStart.begin(), m_groupStart.end() - 1);
    for (std::size_t i = 0; i < count; ++i) {
        if (m_instanceDraws[i].highlight)
            ++m_highlightCursor[m_instanceSubmesh[i]];
    }
    m_groupCursor.assign(m_highlightCursor.begin(), m_highlightCursor.end());  // first plain slot
    std::copy(m_groupStart.begin(), m_groupStart.end() - 1, m_highlightCursor.begin());
    m_draws.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::vector<GLuint>& cursor = m_instanceDraws[i].highlight ? m_highlightCursor : m_groupCursor;
        m_draws[cursor[m_instanceSubmesh[i]]++] = m_instanceDraws[i];
    }

    m_indirect.clear();
    m_highlightIndirect.clear();
    for (std::size_t s = 0; s < submeshCount; ++s) {
        const GLuint instances = m_groupStart[s + 1] - m_groupStart[s];
        if (instances > 0)
            m_indirect.add(m_submeshes[s].range, instances, m_groupStart[s]);
        const GLuint highlighted = m_highlightCursor[s] - m_groupStart[s];
        if (highlighted > 0)
            m_highlightIndirect.add(m_submeshes[s].range, highlighted, m_groupStart[s]);
    }
    m_indirect.upload();
    m_highlightIndirect.upload();

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
        GpuScope scope(m_profiler, "Upload draws");
        uploadDraws();
        m_drawsDirty = false;
    }
    if (m_lightsDirty) {
        uploadLights();
        m_lightsDirty = false;
    }
    if (m_materialsDirty) {
        streamBuffer(m_materialSsbo, m_materialSsboBytes, m_materials.data(),
                     static_cast<GLsizeiptr>(m_materials.size() * sizeof(GpuMaterial)));
        m_materialsDirty = false;
    }
}

void Renderer::drawScene(const IndirectBuffer& draws) const
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_drawSsbo);
    m_geometry.bind();
    draws.draw();
}

// Shadow map, optional depth prepass, lit scene into the HDR buffer, then one fullscreen pass:
// tonemap + FXAA + sRGB encode
void Renderer::render(const ViewMatrices& view, const RenderTarget* target, int width, int height)
{
    prepare();
    const RenderTarget* scene = target;
    if (!scene) {
        m_windowTarget.resize(width, height);
        scene = &m_windowTarget;
    }
    if (!scene->valid())
        return;

    const bool shadows = m_shadowSettings.enabled && m_sunRadiance != glm::vec3(0.0f);
    if (shadows) {
        GpuScope scope(m_profiler, "Shadow map");
        renderShadowMap(view);
    }
    m_clusters.build(m_lights, view.view, view.proj);
    uploadClusters();

    const glm::mat4 viewProj = view.viewProj();
    const glm::vec4 viewDepth = -glm::vec4(view.view[0][2], view.view[1][2], view.view[2][2], view.view[3][2]);
    // Orthographic view rays are parallel: towards the camera along its +Z
    const glm::vec4 eye = isOrthographic(view.proj) ? glm::vec4(-glm::vec3(viewDepth), 0.0f)
                                                    : glm::vec4(glm::vec3(glm::inverse(view.view)[3]), 1.0f);

    scene->bindHdr();
    glViewport(0, 0, width, height);
    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_depthPrepass) {
        GpuScope scope(m_profiler, "Depth prepass");
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        m_depthShader.use();
        Shader::setMat4(m_depthViewProjLocation, viewProj);
        drawScene(m_indirect);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        // Same vertex shader with invariant gl_Position, so the lit pass hits the exact same depths
        glDepthFunc(GL_EQUAL);
        glDepthMask(GL_FALSE);
    }

    {
        GpuScope scope(m_profiler, "Lit pass");
        m_shader.use();
        Shader::setMat4(m_viewProjLocation, viewProj);
        glUniform4fv(m_eyeLocation, 1, &eye.x);
        glUniform4fv(m_viewDepthLocation, 1, &viewDepth.x);
        glUniform2f(m_clusterTileLocation, static_cast<float>(LightClusters::X) / static_cast<float>(width),
                    static_cast<float>(LightClusters::Y) / static_cast<float>(height));
        glUniform3fv(m_clusterZLocation, 1, &m_clusters.sliceParams().x);
        glUniform3fv(m_sunDirectionLocation, 1, &m_sunDirection.x);
        glUniform3fv(m_sunRadianceLocation, 1, &m_sunRadiance.x);
        glUniform3fv(m_skyColorLocation, 1, &m_skyColor.x);
        glUniform3fv(m_groundColorLocation, 1, &m_groundColor.x);
        glUniform1i(m_shadowsLocation, shadows ? 1 : 0);
        Shader::setMat4(m_shadowMatrixLocation, m_shadowMatrix);
        glUniform1f(m_shadowNormalOffsetLocation, m_shadowNormalOffset);
        glUniform1f(m_shadowDistanceLocation, m_shadowSettings.distance);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_lightSsbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_clusterSsbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_clusterLightSsbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_materialSsbo);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_shadowMap.texture());
        m_textures.bind(0);
        drawScene(m_indirect);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
    }

    {
        GpuScope scope(m_profiler, "Tonemap");
        if (target)
            target->bind();
        else
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // Only the scene is sRGB-encoded; UI drawn afterwards already has sRGB colors
        glEnable(GL_FRAMEBUFFER_SRGB);
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(m_emptyVao);
        m_tonemapShader.use();
        glUniform1f(m_exposureLocation, m_exposure);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, scene->hdrTexture());
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_FRAMEBUFFER_SRGB);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::renderPreview(ModelId model, MaterialId material, const RenderTarget& target, const glm::vec3& from)
{
    if (model >= m_models.size() || !target.valid())
        return;

    m_previewSubmesh.clear();
    m_previewDraws.clear();
    m_previewLights.clear();
    m_instanceSubmesh.swap(m_previewSubmesh);
    m_instanceDraws.swap(m_previewDraws);
    m_lights.swap(m_previewLights);
    const glm::vec3 sunDirection = m_sunDirection, sunRadiance = m_sunRadiance;
    const glm::vec3 sky = m_skyColor, ground = m_groundColor, clear = m_clearColor;
    const float exposure = m_exposure;
    const bool shadows = m_shadowSettings.enabled, prepass = m_depthPrepass;

    const glm::vec3 back = glm::normalize(from);
    const glm::vec3 side = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), back));
    m_sunDirection = glm::normalize(back + side * -0.8f + glm::vec3(0.0f, 1.2f, 0.0f));  // key light, upper left
    m_sunRadiance = glm::vec3(2.6f);
    m_skyColor = glm::vec3(0.40f, 0.42f, 0.48f);
    m_groundColor = glm::vec3(0.12f, 0.11f, 0.10f);
    m_clearColor = glm::vec3(0.028f);
    m_exposure = 1.0f;
    m_shadowSettings.enabled = false;
    m_depthPrepass = false;
    m_lightsDirty = true;

    addInstance(model, glm::mat4(1.0f), PickBuffer::NoId, false, material);

    const Bounds& bounds = m_models[model].bounds;
    const glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
    const float radius = std::max(glm::length(bounds.max - bounds.min) * 0.5f, 1e-3f);
    constexpr float fov = glm::radians(30.0f);
    const float distance = radius / std::sin(fov * 0.5f);
    const glm::vec3 eye = center + back * distance;
    const ViewMatrices view{glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f)),
                            glm::perspective(fov, static_cast<float>(target.width()) / static_cast<float>(target.height()),
                                             std::max(distance - radius * 1.5f, distance * 0.01f), distance + radius * 1.5f)};
    render(view, &target, target.width(), target.height());

    m_instanceSubmesh.swap(m_previewSubmesh);
    m_instanceDraws.swap(m_previewDraws);
    m_lights.swap(m_previewLights);
    m_sunDirection = sunDirection;
    m_sunRadiance = sunRadiance;
    m_skyColor = sky;
    m_groundColor = ground;
    m_clearColor = clear;
    m_exposure = exposure;
    m_shadowSettings.enabled = shadows;
    m_depthPrepass = prepass;
    m_drawsDirty = true;
    m_lightsDirty = true;
}

void Renderer::renderIds(const glm::mat4& viewProj, const PickBuffer& target)
{
    prepare();
    target.bind();
    glViewport(0, 0, target.width(), target.height());
    target.clear();
    m_pickShader.use();
    Shader::setMat4(m_pickViewProjLocation, viewProj);
    drawScene(m_indirect);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::renderMask(const glm::mat4& viewProj, const RenderTarget& target, OutlineMask& mask)
{
    prepare();
    mask.resize(target);
    if (!mask.valid())
        return;
    mask.bindMask();
    glViewport(0, 0, mask.width(), mask.height());
    const GLfloat zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearBufferfv(GL_COLOR, 0, zero);
    if (!hasHighlight()) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    m_maskShader.use();
    Shader::setMat4(m_maskViewProjLocation, viewProj);
    drawScene(m_highlightIndirect);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Separable box dilation: a horizontal pass into the mask's second texture, then vertical onto target
void Renderer::drawOutline(const RenderTarget& target, const OutlineMask& mask, const glm::vec4& color, int width)
{
    if (!hasHighlight() || !mask.valid())
        return;
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(m_emptyVao);

    mask.bindDilated();
    glViewport(0, 0, mask.width(), mask.height());
    m_dilateShader.use();
    glUniform1i(m_dilateWidthLocation, width);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mask.maskTexture());
    glDrawArrays(GL_TRIANGLES, 0, 3);

    target.bind();
    glViewport(0, 0, target.width(), target.height());
    glEnable(GL_FRAMEBUFFER_SRGB);
    m_outlineShader.use();
    glUniform4fv(m_outlineColorLocation, 1, &color.x);
    glUniform1i(m_outlineWidthLocation, width);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mask.dilatedTexture());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::reloadShaders()
{
    const bool mesh = m_shader.reload();
    const bool pick = m_pickShader.reload();
    const bool mask = m_maskShader.reload();
    const bool dilate = m_dilateShader.reload();
    const bool outline = m_outlineShader.reload();
    const bool tonemap = m_tonemapShader.reload();
    const bool depth = m_depthShader.reload();
    if (mesh || pick || mask || dilate || outline || tonemap || depth)
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
    const bool mask = m_maskShader.reloadIfChanged();
    const bool dilate = m_dilateShader.reloadIfChanged();
    const bool outline = m_outlineShader.reloadIfChanged();
    const bool tonemap = m_tonemapShader.reloadIfChanged();
    const bool depth = m_depthShader.reloadIfChanged();
    if (mesh || pick || mask || dilate || outline || tonemap || depth) {
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
    s.materials = m_materials.size();
    s.lights = m_lights.size();
    s.clusterRefs = m_clusters.indices().size();
    return s;
}
