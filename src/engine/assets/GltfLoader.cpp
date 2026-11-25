#include "GltfLoader.h"

#include "engine/core/Paths.h"

#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cstring>

static const cgltf_accessor* findAttribute(const cgltf_primitive& prim, cgltf_attribute_type type)
{
    for (cgltf_size i = 0; i < prim.attributes_count; ++i)
        if (prim.attributes[i].type == type && prim.attributes[i].index == 0)
            return prim.attributes[i].data;
    return nullptr;
}

static std::optional<ImageData> loadImage(const cgltf_image& image, const std::string& gltfPath)
{
    if (image.buffer_view)
        return loadImageFromMemory(cgltf_buffer_view_data(image.buffer_view), image.buffer_view->size);

    if (image.uri && std::strncmp(image.uri, "data:", 5) != 0) {
        std::string uri = image.uri;
        uri.resize(cgltf_decode_uri(uri.data()));
        return loadImage(directoryOf(gltfPath) + uri);
    }

    std::fprintf(stderr, "Unsupported image source in %s\n", gltfPath.c_str());
    return std::nullopt;
}

namespace {

class GltfBuilder
{
private:
    const cgltf_data* m_data;
    const std::string& m_path;
    std::vector<int> m_imageToTexture;  // -1 = not loaded yet, -2 = failed
    ModelData m_model;

    int textureFor(const cgltf_material* material)
    {
        if (!material)
            return -1;
        const cgltf_texture* texture = material->pbr_metallic_roughness.base_color_texture.texture;
        if (!texture || !texture->image)
            return -1;

        const auto imageIndex = static_cast<std::size_t>(texture->image - m_data->images);
        int& slot = m_imageToTexture[imageIndex];
        if (slot == -1) {
            std::optional<ImageData> image = loadImage(*texture->image, m_path);
            if (image) {
                slot = static_cast<int>(m_model.textures.size());
                m_model.textures.push_back(std::move(*image));
            } else {
                slot = -2;
            }
        }
        return slot >= 0 ? slot : -1;
    }

public:
    GltfBuilder(const cgltf_data* data, const std::string& path)
        : m_data(data), m_path(path), m_imageToTexture(data->images_count, -1)
    {
    }

    void addPrimitive(const cgltf_primitive& prim, const glm::mat4& world)
    {
        const cgltf_accessor* positions = findAttribute(prim, cgltf_attribute_type_position);
        if (prim.type != cgltf_primitive_type_triangles || !positions)
            return;
        const cgltf_accessor* normals = findAttribute(prim, cgltf_attribute_type_normal);
        const cgltf_accessor* uvs = findAttribute(prim, cgltf_attribute_type_texcoord);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));

        MeshData mesh;
        mesh.vertices.reserve(positions->count);
        for (cgltf_size i = 0; i < positions->count; ++i) {
            Vertex v{};
            cgltf_accessor_read_float(positions, i, glm::value_ptr(v.position), 3);
            v.position = glm::vec3(world * glm::vec4(v.position, 1.0f));
            if (normals) {
                cgltf_accessor_read_float(normals, i, glm::value_ptr(v.normal), 3);
                v.normal = glm::normalize(normalMatrix * v.normal);
            }
            if (uvs)
                cgltf_accessor_read_float(uvs, i, glm::value_ptr(v.uv), 2);
            mesh.vertices.push_back(v);
        }

        if (prim.indices) {
            mesh.indices.reserve(prim.indices->count);
            for (cgltf_size i = 0; i < prim.indices->count; ++i)
                mesh.indices.push_back(static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, i)));
        } else {
            for (cgltf_size i = 0; i < positions->count; ++i)
                mesh.indices.push_back(static_cast<std::uint32_t>(i));
        }

        mesh.texture = textureFor(prim.material);
        m_model.meshes.push_back(std::move(mesh));
    }

    ModelData take() { return std::move(m_model); }
};

}

std::optional<ModelData> loadGltf(const std::string& path)
{
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
        std::fprintf(stderr, "Failed to parse %s\n", path.c_str());
        return std::nullopt;
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success
        || cgltf_validate(data) != cgltf_result_success) {
        std::fprintf(stderr, "Failed to load buffers for %s\n", path.c_str());
        cgltf_free(data);
        return std::nullopt;
    }

    GltfBuilder builder(data, path);
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh)
            continue;
        float world[16];
        cgltf_node_transform_world(&node, world);
        for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
            builder.addPrimitive(node.mesh->primitives[p], glm::make_mat4(world));
    }
    cgltf_free(data);

    ModelData model = builder.take();
    if (model.meshes.empty()) {
        std::fprintf(stderr, "No triangle meshes in %s\n", path.c_str());
        return std::nullopt;
    }
    return model;
}
