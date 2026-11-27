#include "Renderer.h"

#include "engine/anim/Pose.h"
#include "engine/core/Paths.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstddef>
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
      m_skinned(config.vertices / 4, config.indices / 4, config.draws),
      m_indirect(config.draws),
      m_highlightIndirect(config.draws),
      m_skinnedIndirect(config.draws),
      m_skinnedHighlightIndirect(config.draws),
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
        !renderer->m_depthShader.load(meshVert, resourcePath("shaders/depth.frag")) ||
        !renderer->m_skinShader.loadCompute(resourcePath("shaders/skin.comp")) ||
        !renderer->m_spriteShader.load(resourcePath("shaders/sprite.vert"), resourcePath("shaders/sprite.frag")) ||
        !renderer->m_spritePickShader.load(resourcePath("shaders/sprite.vert"), resourcePath("shaders/spritePick.frag")))
        return nullptr;
    renderer->onShadersLoaded();

    glGenVertexArrays(1, &renderer->m_spriteVao);
    glGenBuffers(1, &renderer->m_spriteVbo);
    glBindVertexArray(renderer->m_spriteVao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->m_spriteVbo);
    constexpr GLsizei stride = sizeof(SpriteVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SpriteVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SpriteVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SpriteVertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SpriteVertex, color)));
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 2, GL_UNSIGNED_INT, stride, reinterpret_cast<void*>(offsetof(SpriteVertex, flags)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
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
    glDeleteBuffers(1, &m_spriteVbo);
    glDeleteVertexArrays(1, &m_spriteVao);
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
    if (!m_depthShader.lastError().empty())
        return m_depthShader.lastError();
    return m_skinShader.lastError();
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

    ModelEntry entry{};
    const SkeletonData* skeleton = model.skeleton ? &*model.skeleton : nullptr;
    if (skeleton) {
        entry.jointCount = static_cast<std::uint32_t>(skeleton->jointCount());
        entry.bindPalette.resize(skeleton->jointCount() * 3);
        writePalette(*skeleton, skeleton->bindModel, entry.bindPalette);
    }

    Bounds bounds{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
    entry.firstSubmesh = static_cast<std::uint32_t>(m_submeshes.size());
    entry.firstSkinned = static_cast<std::uint32_t>(m_skinnedSubmeshes.size());
    std::vector<Vertex> skinVertices;
    std::vector<glm::u8vec4> skinJoints, skinWeights;
    for (const MeshData& mesh : model.meshes) {
        if (mesh.indices.empty())
            continue;
        const bool hasMaterial = mesh.material >= 0 && static_cast<std::size_t>(mesh.material) < materials.size();
        const MaterialId material = hasMaterial ? materials[static_cast<std::size_t>(mesh.material)] : DefaultMaterial;
        const bool skinned = skeleton && mesh.skinned() && mesh.joints.size() == mesh.vertices.size() &&
                             mesh.weights.size() == mesh.vertices.size();

        // Skinned meshes also get a static bind-pose copy, for plain MeshRenderers and thumbnails
        std::vector<Vertex> bindPose;
        if (skinned) {
            bindPose.resize(mesh.vertices.size());
            for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
                glm::vec4 rows[3] = {glm::vec4(0.0f), glm::vec4(0.0f), glm::vec4(0.0f)};
                for (int c = 0; c < 4; ++c) {
                    const float w = static_cast<float>(mesh.weights[i][c]) / 255.0f;
                    const std::size_t joint = std::min<std::size_t>(mesh.joints[i][c], skeleton->jointCount() - 1);
                    for (int r = 0; r < 3; ++r)
                        rows[r] += entry.bindPalette[joint * 3 + static_cast<std::size_t>(r)] * w;
                }
                const Vertex& v = mesh.vertices[i];
                const glm::vec4 p(v.position, 1.0f);
                const glm::vec3 n(glm::dot(glm::vec3(rows[0]), v.normal), glm::dot(glm::vec3(rows[1]), v.normal),
                                  glm::dot(glm::vec3(rows[2]), v.normal));
                bindPose[i] = {{glm::dot(rows[0], p), glm::dot(rows[1], p), glm::dot(rows[2], p)},
                               glm::length(n) > 1e-8f ? glm::normalize(n) : v.normal, v.uv};
            }
        }
        const std::vector<Vertex>& vertices = skinned ? bindPose : mesh.vertices;
        for (const Vertex& v : vertices) {
            bounds.min = glm::min(bounds.min, v.position);
            bounds.max = glm::max(bounds.max, v.position);
        }
        m_submeshes.push_back({m_geometry.addMesh(vertices, mesh.indices), material, skinned});

        if (skinned) {
            const GLuint firstIndex = m_skinned.addIndices(mesh.indices);
            m_skinnedSubmeshes.push_back({firstIndex, static_cast<GLuint>(mesh.indices.size()),
                                          static_cast<GLuint>(skinVertices.size()), material});
            skinVertices.insert(skinVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            skinJoints.insert(skinJoints.end(), mesh.joints.begin(), mesh.joints.end());
            skinWeights.insert(skinWeights.end(), mesh.weights.begin(), mesh.weights.end());
        }
    }
    entry.submeshCount = static_cast<std::uint32_t>(m_submeshes.size()) - entry.firstSubmesh;
    entry.skinnedCount = static_cast<std::uint32_t>(m_skinnedSubmeshes.size()) - entry.firstSkinned;
    if (entry.submeshCount == 0)
        return InvalidModel;
    if (entry.skinnedCount > 0) {
        entry.sourceFirst = m_skinned.addSource(skinVertices, skinJoints, skinWeights);
        entry.skinnedVertices = static_cast<GLuint>(skinVertices.size());
    } else {
        entry.bindPalette.clear();
        entry.jointCount = 0;
    }
    entry.bounds = bounds;
    m_models.push_back(std::move(entry));
    return static_cast<ModelId>(m_models.size() - 1);
}

static constexpr std::uint32_t SkinIndexBits = 20;
static constexpr std::uint32_t SkinIndexMask = (1u << SkinIndexBits) - 1;

Renderer::Skin* Renderer::skinSlot(SkinId id)
{
    if (id == InvalidSkin)
        return nullptr;
    const std::uint32_t index = id & SkinIndexMask;
    if (index >= m_skins.size())
        return nullptr;
    Skin& skin = m_skins[index];
    if (skin.model == InvalidModel || skin.generation != (id >> SkinIndexBits))
        return nullptr;
    return &skin;
}

void Renderer::releaseSkin(std::uint32_t index)
{
    Skin& skin = m_skins[index];
    const ModelEntry& model = m_models[skin.model];
    m_skinned.freeOutput(skin.output, model.skinnedVertices);
    m_paletteJoints.free(skin.jointBase, model.jointCount);
    skin.model = InvalidModel;
    skin.generation = (skin.generation + 1) & (UINT32_MAX >> SkinIndexBits);
    skin.dirty = false;
    skin.used = false;
    m_freeSkins.push_back(index);
}

bool Renderer::acquireSkin(SkinId& id, ModelId model)
{
    if (Skin* skin = skinSlot(id); skin && skin->model == model) {
        skin->used = true;
        return false;
    }
    id = InvalidSkin;  // a skin of another model is left to beginFrame, like any unused one
    if (!modelSkinned(model))
        return false;

    std::uint32_t index;
    if (!m_freeSkins.empty()) {
        index = m_freeSkins.back();
        m_freeSkins.pop_back();
    } else {
        index = static_cast<std::uint32_t>(m_skins.size());
        if (index > SkinIndexMask)
            return false;
        m_skins.emplace_back();
    }
    const ModelEntry& entry = m_models[model];
    Skin& skin = m_skins[index];
    skin.model = model;
    skin.output = m_skinned.allocateOutput(entry.skinnedVertices);
    skin.jointBase = m_paletteJoints.allocate(entry.jointCount);
    skin.used = true;
    skin.dirty = true;
    m_palette.resize(static_cast<std::size_t>(m_paletteJoints.end()) * 3);
    std::copy(entry.bindPalette.begin(), entry.bindPalette.end(),
              m_palette.begin() + static_cast<std::ptrdiff_t>(skin.jointBase) * 3);
    id = index | skin.generation << SkinIndexBits;
    return true;
}

std::span<glm::vec4> Renderer::writeSkinPalette(SkinId id)
{
    Skin* skin = skinSlot(id);
    if (!skin)
        return {};
    skin->dirty = true;
    return std::span<glm::vec4>(m_palette).subspan(static_cast<std::size_t>(skin->jointBase) * 3,
                                                   static_cast<std::size_t>(m_models[skin->model].jointCount) * 3);
}

void Renderer::addSkinnedInstance(SkinId id, const glm::mat4& transform, std::uint32_t entityId, bool highlight,
                                  MaterialId material)
{
    const Skin* skin = skinSlot(id);
    if (!skin)
        return;
    const ModelEntry& entry = m_models[skin->model];
    const bool overridden = material < m_materials.size();
    DrawData draw{};
    draw.setModel(transform);
    draw.entityId = entityId;
    draw.highlight = highlight ? 1u : 0u;
    for (std::uint32_t i = 0; i < entry.submeshCount; ++i) {
        const std::uint32_t submesh = entry.firstSubmesh + i;
        if (m_submeshes[submesh].bindPose)
            continue;
        draw.material = overridden ? material : m_submeshes[submesh].material;
        m_instanceSubmesh.push_back(submesh);
        m_instanceDraws.push_back(draw);
    }
    for (std::uint32_t i = 0; i < entry.skinnedCount; ++i) {
        const SkinnedSubmesh& submesh = m_skinnedSubmeshes[entry.firstSkinned + i];
        draw.material = overridden ? material : submesh.material;
        m_skinnedRanges.push_back({submesh.firstIndex, submesh.indexCount,
                                   static_cast<GLint>(skin->output + submesh.vertexOffset)});
        m_skinnedDraws.push_back(draw);
    }
    m_drawsDirty = true;
}

void Renderer::beginFrame()
{
    for (std::uint32_t i = 0; i < m_skins.size(); ++i) {
        Skin& skin = m_skins[i];
        if (skin.model == InvalidModel)
            continue;
        if (!skin.used)
            releaseSkin(i);
        else
            skin.used = false;
    }

    m_skinJobs.clear();
    m_skinnedVerticesLastFrame = 0;
    for (Skin& skin : m_skins) {
        if (skin.model == InvalidModel || !skin.dirty)
            continue;
        const ModelEntry& model = m_models[skin.model];
        m_skinJobs.push_back({model.sourceFirst, skin.output, model.skinnedVertices, skin.jointBase, 0, {}});
        m_skinnedVerticesLastFrame += model.skinnedVertices;
        skin.dirty = false;
    }
    if (m_skinJobs.empty())
        return;
    GpuScope scope(m_profiler, "Skinning");
    m_skinShader.use();
    m_skinned.skin(std::span<const glm::vec4>(m_palette.data(), static_cast<std::size_t>(m_paletteJoints.end()) * 3),
                   m_skinJobs);
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
    m_skinnedRanges.clear();
    m_skinnedDraws.clear();
    m_sprites.clear();
    m_drawsDirty = true;
}

void Renderer::buildSprites(const glm::vec4* sortDepth)
{
    m_spriteOrder.resize(m_sprites.size());
    for (std::uint32_t i = 0; i < m_spriteOrder.size(); ++i)
        m_spriteOrder[i] = i;
    if (sortDepth) {
        const glm::vec4 d = *sortDepth;
        const auto depth = [&](const SpriteDraw& s) { return glm::dot(glm::vec3(d), glm::vec3(s.world[3])) + d.w; };
        std::stable_sort(m_spriteOrder.begin(), m_spriteOrder.end(), [&](std::uint32_t a, std::uint32_t b) {
            const SpriteDraw& sa = m_sprites[a];
            const SpriteDraw& sb = m_sprites[b];
            if (sa.sortOrder != sb.sortOrder)
                return sa.sortOrder < sb.sortOrder;
            return depth(sa) > depth(sb);
        });
    }

    m_spriteVertices.clear();
    m_spriteBatches.clear();
    for (const std::uint32_t index : m_spriteOrder) {
        const SpriteDraw& s = m_sprites[index];
        const glm::vec3 x = glm::vec3(s.world[0]) * s.size.x;
        const glm::vec3 y = glm::vec3(s.world[1]) * s.size.y;
        const glm::vec3 centre(s.world[3]);
        const glm::vec3 cross = glm::cross(x, y);
        const glm::vec3 normal = glm::dot(cross, cross) > 1e-12f ? glm::normalize(cross) : glm::vec3(0.0f, 0.0f, 1.0f);
        const std::uint32_t flags = (s.lit ? 1u : 0u) | (s.receiveShadows ? 2u : 0u);
        const auto vertex = [&](float fx, float fy, float u, float v) {
            return SpriteVertex{centre + x * fx + y * fy, normal, {u, v}, s.color, flags, s.entityId};
        };
        const SpriteVertex topLeft = vertex(-0.5f, 0.5f, s.uv.x, s.uv.y);
        const SpriteVertex topRight = vertex(0.5f, 0.5f, s.uv.z, s.uv.y);
        const SpriteVertex bottomRight = vertex(0.5f, -0.5f, s.uv.z, s.uv.w);
        const SpriteVertex bottomLeft = vertex(-0.5f, -0.5f, s.uv.x, s.uv.w);
        if (m_spriteBatches.empty() || m_spriteBatches.back().texture != s.texture)
            m_spriteBatches.push_back({s.texture, static_cast<GLint>(m_spriteVertices.size()), 0});
        m_spriteVertices.insert(m_spriteVertices.end(), {bottomLeft, bottomRight, topRight, bottomLeft, topRight, topLeft});
        m_spriteBatches.back().count += 6;
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_spriteVbo);
    const auto bytes = static_cast<GLsizeiptr>(m_spriteVertices.size() * sizeof(SpriteVertex));
    glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, m_spriteVertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Renderer::drawSprites() const
{
    glBindVertexArray(m_spriteVao);
    glActiveTexture(GL_TEXTURE0);
    for (const SpriteBatch& batch : m_spriteBatches) {
        glBindTexture(GL_TEXTURE_2D, batch.texture);
        glDrawArrays(GL_TRIANGLES, batch.first, batch.count);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
}

void Renderer::setLightingUniforms(const Shader& shader, const glm::mat4& viewProj, const glm::vec4& eye,
                                   const glm::vec4& viewDepth, int width, int height, bool shadows) const
{
    Shader::setMat4(shader.uniformLocation("uViewProj"), viewProj);
    glUniform4fv(shader.uniformLocation("uEye"), 1, &eye.x);
    glUniform4fv(shader.uniformLocation("uViewDepth"), 1, &viewDepth.x);
    glUniform2f(shader.uniformLocation("uClusterTile"), static_cast<float>(LightClusters::X) / static_cast<float>(width),
                static_cast<float>(LightClusters::Y) / static_cast<float>(height));
    glUniform3fv(shader.uniformLocation("uClusterZ"), 1, &m_clusters.sliceParams().x);
    glUniform3fv(shader.uniformLocation("uSunDirection"), 1, &m_sunDirection.x);
    glUniform3fv(shader.uniformLocation("uSunRadiance"), 1, &m_sunRadiance.x);
    glUniform3fv(shader.uniformLocation("uSkyColor"), 1, &m_skyColor.x);
    glUniform3fv(shader.uniformLocation("uGroundColor"), 1, &m_groundColor.x);
    glUniform1i(shader.uniformLocation("uShadows"), shadows ? 1 : 0);
    Shader::setMat4(shader.uniformLocation("uShadowMatrix"), m_shadowMatrix);
    glUniform1f(shader.uniformLocation("uShadowNormalOffset"), m_shadowNormalOffset);
    glUniform1f(shader.uniformLocation("uShadowDistance"), m_shadowSettings.distance);
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
    drawScene(false);
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

    // Skinned slots follow the static ones: one command each, its own output region
    m_skinnedIndirect.clear();
    m_skinnedHighlightIndirect.clear();
    for (std::size_t k = 0; k < m_skinnedDraws.size(); ++k) {
        const auto slot = static_cast<GLuint>(count + k);
        m_skinnedIndirect.add(m_skinnedRanges[k], 1, slot);
        if (m_skinnedDraws[k].highlight)
            m_skinnedHighlightIndirect.add(m_skinnedRanges[k], 1, slot);
    }
    m_skinnedIndirect.upload();
    m_skinnedHighlightIndirect.upload();
    m_draws.insert(m_draws.end(), m_skinnedDraws.begin(), m_skinnedDraws.end());

    const auto slots = static_cast<GLsizeiptr>(m_draws.size());
    m_geometry.reserveDraws(slots);
    m_skinned.reserveDraws(slots);
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

void Renderer::drawScene(bool highlightedOnly) const
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_drawSsbo);
    m_geometry.bind();
    (highlightedOnly ? m_highlightIndirect : m_indirect).draw();
    if ((highlightedOnly ? m_skinnedHighlightIndirect : m_skinnedIndirect).size() > 0) {
        m_skinned.bind();
        (highlightedOnly ? m_skinnedHighlightIndirect : m_skinnedIndirect).draw();
    }
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
        drawScene(false);
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
        drawScene(false);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
    }

    if (!m_sprites.empty()) {
        GpuScope scope(m_profiler, "Sprites");
        buildSprites(&viewDepth);
        m_spriteShader.use();
        setLightingUniforms(m_spriteShader, viewProj, eye, viewDepth, width, height, shadows);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        drawSprites();
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
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
    m_previewSkinnedRanges.clear();
    m_previewSkinnedDraws.clear();
    m_skinnedRanges.swap(m_previewSkinnedRanges);
    m_skinnedDraws.swap(m_previewSkinnedDraws);
    m_lights.swap(m_previewLights);
    m_previewSprites.clear();
    m_sprites.swap(m_previewSprites);
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
    m_skinnedRanges.swap(m_previewSkinnedRanges);
    m_skinnedDraws.swap(m_previewSkinnedDraws);
    m_lights.swap(m_previewLights);
    m_sprites.swap(m_previewSprites);
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
    drawScene(false);
    if (!m_sprites.empty()) {
        buildSprites(nullptr);
        m_spritePickShader.use();
        Shader::setMat4(m_spritePickShader.uniformLocation("uViewProj"), viewProj);
        drawSprites();
    }
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
    drawScene(true);
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
    m_skinShader.reload();
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
    const bool skin = m_skinShader.reloadIfChanged();
    const bool sprite = m_spriteShader.reloadIfChanged() | m_spritePickShader.reloadIfChanged();
    if (sprite)
        std::printf("Sprite shaders reloaded\n");
    if (skin) {
        for (Skin& s : m_skins)
            s.dirty = s.model != InvalidModel;
        std::printf("Skinning shader reloaded\n");
    }
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
    s.draws = m_indirect.size() + m_skinnedIndirect.size();
    s.drawCapacity = m_indirect.capacity();
    s.textureLayers = m_textures.layerCount();
    s.textureLayerCapacity = m_textures.layerCapacity();
    s.textureSize = m_textures.layerSize();
    s.models = m_models.size();
    s.materials = m_materials.size();
    s.lights = m_lights.size();
    s.clusterRefs = m_clusters.indices().size();
    s.skins = m_skins.size() - m_freeSkins.size();
    s.skinnedVertices = m_skinned.outputVertexCount();
    s.skinnedThisFrame = m_skinnedVerticesLastFrame;
    return s;
}
