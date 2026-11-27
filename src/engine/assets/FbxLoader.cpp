#include "FbxLoader.h"

#include "engine/core/Paths.h"

#include <ufbx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

static glm::mat4 toGlm(const ufbx_matrix& m)
{
    return glm::mat4(
        glm::vec4(m.cols[0].x, m.cols[0].y, m.cols[0].z, 0.0f),
        glm::vec4(m.cols[1].x, m.cols[1].y, m.cols[1].z, 0.0f),
        glm::vec4(m.cols[2].x, m.cols[2].y, m.cols[2].z, 0.0f),
        glm::vec4(m.cols[3].x, m.cols[3].y, m.cols[3].z, 1.0f));
}

static std::optional<ImageData> loadTexture(const ufbx_texture& texture, const std::string& fbxPath)
{
    if (texture.content.size > 0)
        return loadImageFromMemory(static_cast<const unsigned char*>(texture.content.data), texture.content.size);

    if (texture.relative_filename.length > 0) {
        std::string relative(texture.relative_filename.data, texture.relative_filename.length);
        std::replace(relative.begin(), relative.end(), '\\', '/');
        if (std::optional<ImageData> image = loadImage(directoryOf(fbxPath) + relative))
            return image;
    }
    if (texture.absolute_filename.length > 0)
        return loadImage(std::string(texture.absolute_filename.data, texture.absolute_filename.length));
    return std::nullopt;
}

static const ufbx_texture* baseColorTexture(const ufbx_material* material)
{
    if (!material)
        return nullptr;
    if (material->pbr.base_color.texture)
        return material->pbr.base_color.texture;
    return material->fbx.diffuse_color.texture;
}

// FBX colors are authored in sRGB
static glm::vec4 baseColorOf(const ufbx_material& material)
{
    const ufbx_material_map& map = material.pbr.base_color.has_value ? material.pbr.base_color
                                                                       : material.fbx.diffuse_color;
    if (!map.has_value)
        return glm::vec4(1.0f);
    const auto toLinear = [](ufbx_real c) {
        const float f = static_cast<float>(c);
        return f <= 0.04045f ? f / 12.92f : std::pow((f + 0.055f) / 1.055f, 2.4f);
    };
    return glm::vec4(toLinear(map.value_vec4.x), toLinear(map.value_vec4.y), toLinear(map.value_vec4.z), 1.0f);
}

std::optional<ModelData> loadFbx(const std::string& path)
{
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!scene) {
        std::fprintf(stderr, "Failed to load %s: %s\n", path.c_str(), error.description.data);
        return std::nullopt;
    }

    ModelData model;
    std::unordered_map<const ufbx_texture*, int> textureSlots;  // -1 = failed to load
    std::unordered_map<const ufbx_material*, int> materialSlots;

    for (const ufbx_node* node : scene->nodes) {
        const ufbx_mesh* mesh = node->mesh;
        if (!mesh)
            continue;

        const glm::mat4 world = toGlm(node->geometry_to_world);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
        std::vector<std::uint32_t> triangle(mesh->max_face_triangles * 3);

        for (const ufbx_mesh_part& part : mesh->material_parts) {
            if (part.num_triangles == 0)
                continue;

            std::vector<Vertex> vertices;
            vertices.reserve(part.num_triangles * 3);
            for (std::uint32_t faceIndex : part.face_indices) {
                const std::uint32_t count = ufbx_triangulate_face(triangle.data(), triangle.size(), mesh,
                                                                  mesh->faces.data[faceIndex]);
                for (std::uint32_t i = 0; i < count * 3; ++i) {
                    const std::uint32_t index = triangle[i];
                    Vertex v{};
                    const ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                    v.position = glm::vec3(world * glm::vec4(p.x, p.y, p.z, 1.0f));
                    if (mesh->vertex_normal.exists) {
                        const ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, index);
                        v.normal = glm::normalize(normalMatrix * glm::vec3(n.x, n.y, n.z));
                    }
                    if (mesh->vertex_uv.exists) {
                        const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                        v.uv = glm::vec2(uv.x, 1.0f - uv.y);  // FBX has V up, images are top row first
                    }
                    vertices.push_back(v);
                }
            }

            MeshData out;
            out.indices.resize(vertices.size());
            ufbx_vertex_stream stream{vertices.data(), vertices.size(), sizeof(Vertex)};
            const std::size_t unique = ufbx_generate_indices(&stream, 1, out.indices.data(), out.indices.size(),
                                                             nullptr, &error);
            if (error.type != UFBX_ERROR_NONE) {
                std::fprintf(stderr, "Failed to index mesh in %s: %s\n", path.c_str(), error.description.data);
                continue;
            }
            vertices.resize(unique);
            out.vertices = std::move(vertices);

            const ufbx_material* material = part.index < mesh->materials.count ? mesh->materials.data[part.index] : nullptr;
            if (material) {
                auto [slot, inserted] = materialSlots.try_emplace(material, -1);
                if (inserted) {
                    MaterialData data;
                    if (const ufbx_texture* texture = baseColorTexture(material)) {
                        auto [it, added] = textureSlots.try_emplace(texture, -1);
                        if (added) {
                            if (std::optional<ImageData> image = loadTexture(*texture, path)) {
                                it->second = static_cast<int>(model.textures.size());
                                model.textures.push_back(std::move(*image));
                            }
                        }
                        data.texture = it->second;
                    } else {
                        data.baseColor = baseColorOf(*material);
                    }
                    slot->second = static_cast<int>(model.materials.size());
                    model.materials.push_back(data);
                }
                out.material = slot->second;
            }
            model.meshes.push_back(std::move(out));
        }
    }

    ufbx_free_scene(scene);
    if (model.meshes.empty()) {
        std::fprintf(stderr, "No meshes in %s\n", path.c_str());
        return std::nullopt;
    }
    return model;
}
