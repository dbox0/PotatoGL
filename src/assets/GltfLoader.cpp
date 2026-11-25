#include "GltfLoader.h"

#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>

static const cgltf_accessor* findAttribute(const cgltf_primitive& prim, cgltf_attribute_type type)
{
    for (cgltf_size i = 0; i < prim.attributes_count; ++i)
        if (prim.attributes[i].type == type && prim.attributes[i].index == 0)
            return prim.attributes[i].data;
    return nullptr;
}

static void appendPrimitive(const cgltf_primitive& prim, const glm::mat4& world, MeshData& out)
{
    const cgltf_accessor* positions = findAttribute(prim, cgltf_attribute_type_position);
    if (prim.type != cgltf_primitive_type_triangles || !positions)
        return;
    const cgltf_accessor* normals = findAttribute(prim, cgltf_attribute_type_normal);
    const cgltf_accessor* uvs = findAttribute(prim, cgltf_attribute_type_texcoord);

    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
    const auto base = static_cast<std::uint32_t>(out.vertices.size());

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
        out.vertices.push_back(v);
    }

    if (prim.indices) {
        for (cgltf_size i = 0; i < prim.indices->count; ++i)
            out.indices.push_back(base + static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, i)));
    } else {
        for (cgltf_size i = 0; i < positions->count; ++i)
            out.indices.push_back(base + static_cast<std::uint32_t>(i));
    }
}

std::optional<MeshData> loadGltf(const std::string& path)
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

    MeshData mesh;
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh)
            continue;
        float world[16];
        cgltf_node_transform_world(&node, world);
        for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
            appendPrimitive(node.mesh->primitives[p], glm::make_mat4(world), mesh);
    }

    cgltf_free(data);
    if (mesh.indices.empty()) {
        std::fprintf(stderr, "No triangle meshes in %s\n", path.c_str());
        return std::nullopt;
    }
    return mesh;
}
