#include "GltfLoader.h"

#include "engine/anim/Pose.h"
#include "engine/core/Paths.h"

#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

static const cgltf_accessor* findAttribute(const cgltf_primitive& prim, cgltf_attribute_type type, int index = 0)
{
    for (cgltf_size i = 0; i < prim.attributes_count; ++i)
        if (prim.attributes[i].type == type && prim.attributes[i].index == index)
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

struct SkeletonBuild
{
    SkeletonData skeleton;
    std::vector<int> nodeJoint;  // per node, -1 = not part of the skeleton
};

JointTrs nodeLocal(const cgltf_node& node)
{
    if (node.has_matrix)
        return decompose(Affine::fromMat4(glm::make_mat4(node.matrix)));
    JointTrs trs;
    if (node.has_translation)
        trs.t = glm::make_vec3(node.translation);
    if (node.has_rotation)
        trs.r = glm::normalize(glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]));
    if (node.has_scale)
        trs.s = glm::make_vec3(node.scale);
    return trs;
}

void addJoints(const cgltf_data* data, const cgltf_node* node, const std::vector<char>& used, SkeletonBuild& build)
{
    const auto index = static_cast<std::size_t>(node - data->nodes);
    if (!used[index])
        return;
    SkeletonData& s = build.skeleton;
    build.nodeJoint[index] = static_cast<int>(s.parents.size());
    const int parent = node->parent ? build.nodeJoint[static_cast<std::size_t>(node->parent - data->nodes)] : -1;
    s.parents.push_back(static_cast<std::int16_t>(parent));
    s.bindLocal.push_back(nodeLocal(*node));
    s.names.push_back(node->name ? std::string(node->name) : "joint" + std::to_string(index));
    for (cgltf_size c = 0; c < node->children_count; ++c)
        addJoints(data, node->children[c], used, build);
}

// Every skin's joints plus their ancestors, in depth-first order so parents come first.
// Unskinned mesh nodes in the same hierarchy join too (with their ancestors): they become
// rigid parts weighted fully to themselves, so they follow their bones.
std::optional<SkeletonBuild> buildSkeleton(const cgltf_data* data, const std::string& path)
{
    if (data->skins_count == 0)
        return std::nullopt;
    std::vector<char> used(data->nodes_count, 0);
    for (cgltf_size k = 0; k < data->skins_count; ++k) {
        const cgltf_skin& skin = data->skins[k];
        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            for (const cgltf_node* n = skin.joints[j]; n; n = n->parent)
                used[static_cast<std::size_t>(n - data->nodes)] = 1;
        }
    }
    const std::vector<char> skinned = used;
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh || node.skin)
            continue;
        bool attached = false;
        for (const cgltf_node* p = node.parent; p && !attached; p = p->parent)
            attached = skinned[static_cast<std::size_t>(p - data->nodes)];
        if (attached) {
            for (const cgltf_node* p = &node; p; p = p->parent)
                used[static_cast<std::size_t>(p - data->nodes)] = 1;
        }
    }

    SkeletonBuild build;
    build.nodeJoint.assign(data->nodes_count, -1);
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        if (!data->nodes[n].parent)
            addJoints(data, &data->nodes[n], used, build);
    }
    SkeletonData& s = build.skeleton;
    const std::size_t count = s.parents.size();
    if (count > MaxSkinJoints) {
        std::fprintf(stderr, "Error: %s has %zu joints, skins support at most %zu; loaded without skinning\n",
                     path.c_str(), count, MaxSkinJoints);
        return std::nullopt;
    }

    s.bindModel.resize(count);
    Pose bind;
    bind.setBind(s);
    localToModel(s, bind, s.bindModel);
    for (const std::string& name : s.names)
        s.nameHashes.push_back(jointNameHash(name));

    // Joints no skin uses never deform anything; this keeps their palette entry at identity
    s.inverseBind.resize(count);
    for (std::size_t j = 0; j < count; ++j)
        s.inverseBind[j] = Affine::fromMat4(glm::inverse(s.bindModel[j].toMat4()));
    std::vector<char> assigned(count, 0);
    for (cgltf_size k = 0; k < data->skins_count; ++k) {
        const cgltf_skin& skin = data->skins[k];
        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            const auto joint = static_cast<std::size_t>(build.nodeJoint[static_cast<std::size_t>(skin.joints[j] - data->nodes)]);
            Affine inverseBind;
            if (skin.inverse_bind_matrices) {
                float m[16];
                cgltf_accessor_read_float(skin.inverse_bind_matrices, j, m, 16);
                inverseBind = Affine::fromMat4(glm::make_mat4(m));
            }
            if (assigned[joint]) {
                float diff = 0.0f;
                for (int r = 0; r < 3; ++r)
                    diff = std::max(diff, glm::length(inverseBind.rows[r] - s.inverseBind[joint].rows[r]));
                if (diff > 1e-4f)
                    std::fprintf(stderr, "Warning: %s: skins disagree on the bind pose of joint '%s', using the first\n",
                                 path.c_str(), s.names[joint].c_str());
                continue;
            }
            s.inverseBind[joint] = inverseBind;
            assigned[joint] = 1;
        }
    }
    return build;
}

struct Sampler
{
    std::vector<float> times;
    std::vector<float> values;  // CUBICSPLINE: in-tangent, value, out-tangent per key
    int components = 0;
    cgltf_interpolation_type interpolation = cgltf_interpolation_type_linear;
};

Sampler readSampler(const cgltf_animation_sampler& sampler, int components)
{
    Sampler s;
    s.components = components;
    s.interpolation = sampler.interpolation;
    s.times.resize(sampler.input->count);
    for (cgltf_size i = 0; i < sampler.input->count; ++i)
        cgltf_accessor_read_float(sampler.input, i, &s.times[i], 1);
    s.values.resize(sampler.output->count * static_cast<cgltf_size>(components));
    for (cgltf_size i = 0; i < sampler.output->count; ++i)
        cgltf_accessor_read_float(sampler.output, i, &s.values[i * static_cast<cgltf_size>(components)],
                                  static_cast<cgltf_size>(components));
    const std::size_t perKey = s.interpolation == cgltf_interpolation_type_cubic_spline ? 3 : 1;
    const std::size_t keys = std::min(s.times.size(), s.values.size() / (perKey * static_cast<std::size_t>(components)));
    s.times.resize(keys);
    return s;
}

// The exact value of the sampler at time t, as glTF defines its interpolation
void evaluate(const Sampler& s, float t, float* out)
{
    const int n = s.components;
    const bool cubic = s.interpolation == cgltf_interpolation_type_cubic_spline;
    const std::size_t keyStride = static_cast<std::size_t>(n) * (cubic ? 3 : 1);
    const std::size_t valueOffset = cubic ? static_cast<std::size_t>(n) : 0;
    auto value = [&](std::size_t key) { return s.values.data() + key * keyStride + valueOffset; };
    const std::size_t keys = s.times.size();
    if (keys == 0) {
        return;
    }
    if (keys == 1 || t <= s.times.front()) {
        std::copy_n(value(0), n, out);
        return;
    }
    if (t >= s.times.back()) {
        std::copy_n(value(keys - 1), n, out);
        return;
    }
    const std::size_t k = static_cast<std::size_t>(std::upper_bound(s.times.begin(), s.times.end(), t) - s.times.begin()) - 1;
    const float t0 = s.times[k], t1 = s.times[k + 1];
    const float dt = t1 - t0;
    const float u = dt > 0.0f ? (t - t0) / dt : 0.0f;
    const float* a = value(k);
    const float* b = value(k + 1);

    if (s.interpolation == cgltf_interpolation_type_step) {
        std::copy_n(a, n, out);
        return;
    }
    if (cubic) {
        const float* outTangent = s.values.data() + k * keyStride + 2 * static_cast<std::size_t>(n);
        const float* inTangent = s.values.data() + (k + 1) * keyStride;
        const float u2 = u * u, u3 = u2 * u;
        const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f, h10 = u3 - 2.0f * u2 + u;
        const float h01 = -2.0f * u3 + 3.0f * u2, h11 = u3 - u2;
        for (int c = 0; c < n; ++c)
            out[c] = h00 * a[c] + h10 * dt * outTangent[c] + h01 * b[c] + h11 * dt * inTangent[c];
        if (n == 4) {
            glm::vec4 q = glm::make_vec4(out);
            q = glm::length(q) > 1e-8f ? glm::normalize(q) : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            std::copy_n(&q.x, 4, out);
        }
        return;
    }
    if (n == 4) {
        const glm::quat qa(a[3], a[0], a[1], a[2]), qb(b[3], b[0], b[1], b[2]);
        const glm::quat q = glm::slerp(qa, qb, u);
        out[0] = q.x;
        out[1] = q.y;
        out[2] = q.z;
        out[3] = q.w;
        return;
    }
    for (int c = 0; c < n; ++c)
        out[c] = a[c] + (b[c] - a[c]) * u;
}

ClipData buildClip(const cgltf_data* data, const cgltf_animation& animation, const SkeletonBuild& build,
                   std::string name, const std::string& path)
{
    const SkeletonData& skeleton = build.skeleton;
    struct Channels
    {
        const cgltf_animation_sampler* samplers[3] = {};  // translation, rotation, scale
    };
    std::vector<Channels> joints(skeleton.jointCount());
    std::size_t dropped = 0;
    float duration = 0.0f;
    for (cgltf_size c = 0; c < animation.channels_count; ++c) {
        const cgltf_animation_channel& channel = animation.channels[c];
        int slot = -1;
        if (channel.target_path == cgltf_animation_path_type_translation)
            slot = 0;
        else if (channel.target_path == cgltf_animation_path_type_rotation)
            slot = 1;
        else if (channel.target_path == cgltf_animation_path_type_scale)
            slot = 2;
        if (slot < 0 || !channel.target_node || !channel.sampler || !channel.sampler->input || !channel.sampler->output)
            continue;
        const int joint = build.nodeJoint[static_cast<std::size_t>(channel.target_node - data->nodes)];
        if (joint < 0) {
            ++dropped;
            continue;
        }
        joints[static_cast<std::size_t>(joint)].samplers[slot] = channel.sampler;
        const cgltf_accessor* input = channel.sampler->input;
        if (input->count > 0) {
            float last = 0.0f;
            cgltf_accessor_read_float(input, input->count - 1, &last, 1);
            duration = std::max(duration, last);
        }
    }
    if (dropped > 0)
        std::fprintf(stderr, "Warning: %s, clip '%s': %zu channels animate nodes outside the skeleton, dropped\n",
                     path.c_str(), name.c_str(), dropped);

    ClipData clip;
    clip.name = std::move(name);
    clip.duration = duration;
    clip.frameCount = static_cast<std::uint32_t>(std::ceil(duration * clip.rate - 1e-3f)) + 1;
    const std::uint32_t frames = clip.frameCount;

    struct Built
    {
        ClipData::Track track;
        std::vector<float> channel[3];
    };
    std::vector<Built> built;
    static constexpr int Components[3] = {3, 4, 3};
    for (std::size_t j = 0; j < joints.size(); ++j) {
        const Channels& channels = joints[j];
        if (!channels.samplers[0] && !channels.samplers[1] && !channels.samplers[2])
            continue;
        Built b;
        b.track.jointHash = skeleton.nameHashes[j];
        b.track.constant = skeleton.bindLocal[j];
        for (int slot = 0; slot < 3; ++slot) {
            if (!channels.samplers[slot])
                continue;
            const int n = Components[slot];
            const Sampler sampler = readSampler(*channels.samplers[slot], n);
            std::vector<float> values(static_cast<std::size_t>(frames) * n);
            for (std::uint32_t f = 0; f < frames; ++f) {
                const float t = std::min(static_cast<float>(f) / clip.rate, duration);
                float* out = values.data() + static_cast<std::size_t>(f) * n;
                evaluate(sampler, t, out);
                // Neighbouring rotations in one hemisphere, so playback can nlerp without a sign check
                if (n == 4 && f > 0) {
                    const float* prev = out - 4;
                    if (out[0] * prev[0] + out[1] * prev[1] + out[2] * prev[2] + out[3] * prev[3] < 0.0f) {
                        for (int c = 0; c < 4; ++c)
                            out[c] = -out[c];
                    }
                }
            }
            bool constant = true;
            for (std::size_t i = static_cast<std::size_t>(n); i < values.size() && constant; ++i)
                constant = std::abs(values[i] - values[i % static_cast<std::size_t>(n)]) <= 1e-6f;
            if (constant) {
                if (slot == 0)
                    b.track.constant.t = glm::make_vec3(values.data());
                else if (slot == 1)
                    b.track.constant.r = glm::normalize(glm::quat(values[3], values[0], values[1], values[2]));
                else
                    b.track.constant.s = glm::make_vec3(values.data());
                continue;
            }
            b.track.animated |= static_cast<std::uint8_t>(1 << slot);
            b.channel[slot] = std::move(values);
        }
        built.push_back(std::move(b));
    }

    for (Built& b : built) {
        b.track.offset = clip.frameStride;
        for (int slot = 0; slot < 3; ++slot) {
            if (b.track.animated & (1 << slot))
                clip.frameStride += static_cast<std::uint32_t>(Components[slot]);
        }
        clip.tracks.push_back(b.track);
    }
    clip.frames.resize(static_cast<std::size_t>(frames) * clip.frameStride);
    for (const Built& b : built) {
        std::uint32_t offset = b.track.offset;
        for (int slot = 0; slot < 3; ++slot) {
            if (!(b.track.animated & (1 << slot)))
                continue;
            const int n = Components[slot];
            for (std::uint32_t f = 0; f < frames; ++f)
                std::copy_n(b.channel[slot].data() + static_cast<std::size_t>(f) * n, n,
                            clip.frames.data() + static_cast<std::size_t>(f) * clip.frameStride + offset);
            offset += static_cast<std::uint32_t>(n);
        }
    }
    return clip;
}

// Top four influences, quantized to unorm8 summing to exactly 255. Rounding down and giving the
// remainder to the largest keeps them sorted.
void quantizeInfluences(const cgltf_uint* joints, const float* weights, int count, const std::vector<std::uint8_t>& remap,
                        glm::u8vec4& outJoints, glm::u8vec4& outWeights)
{
    struct Influence
    {
        float weight;
        std::uint8_t joint;
    };
    Influence influences[8];
    int n = 0;
    for (int i = 0; i < count; ++i) {
        if (weights[i] > 0.0f)
            influences[n++] = {weights[i], joints[i] < remap.size() ? remap[joints[i]] : std::uint8_t(0)};
    }
    std::sort(influences, influences + n, [](const Influence& a, const Influence& b) { return a.weight > b.weight; });
    n = std::min(n, 4);
    outJoints = glm::u8vec4(0);
    outWeights = glm::u8vec4(0);
    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += influences[i].weight;
    if (n == 0 || sum <= 0.0f) {
        outWeights[0] = 255;
        return;
    }
    int total = 0;
    for (int i = 0; i < n; ++i) {
        const int q = static_cast<int>(influences[i].weight / sum * 255.0f);
        outJoints[i] = influences[i].joint;
        outWeights[i] = static_cast<std::uint8_t>(std::clamp(q, 0, 255));
        total += outWeights[i];
    }
    outWeights[0] = static_cast<std::uint8_t>(outWeights[0] + (255 - total));
}

}  // namespace

namespace {

class GltfBuilder
{
private:
    const cgltf_data* m_data;
    const std::string& m_path;
    std::vector<int> m_imageToTexture;  // -1 = not loaded yet, -2 = failed
    std::vector<int> m_materialSlots;   // -1 = not created yet
    ModelData m_model;

    int materialFor(const cgltf_material* material)
    {
        if (!material)
            return -1;
        int& slot = m_materialSlots[static_cast<std::size_t>(material - m_data->materials)];
        if (slot >= 0)
            return slot;

        MaterialData out;
        out.texture = textureFor(material);
        if (material->has_pbr_metallic_roughness) {
            const cgltf_pbr_metallic_roughness& pbr = material->pbr_metallic_roughness;
            out.baseColor = glm::make_vec4(pbr.base_color_factor);
            // Metallic/roughness maps aren't supported; their factors alone usually mean "fully metal"
            if (!pbr.metallic_roughness_texture.texture) {
                out.metallic = pbr.metallic_factor;
                out.smoothness = 1.0f - pbr.roughness_factor;
            }
        }
        if (!material->emissive_texture.texture)
            out.emission = glm::make_vec3(material->emissive_factor);
        slot = static_cast<int>(m_model.materials.size());
        m_model.materials.push_back(out);
        return slot;
    }

    int textureFor(const cgltf_material* material)
    {
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
        : m_data(data), m_path(path), m_imageToTexture(data->images_count, -1),
          m_materialSlots(data->materials_count, -1)
    {
    }

    // remap: skin joint index to skeleton joint; empty for a static mesh, which gets world baked in
    void addPrimitive(const cgltf_primitive& prim, const glm::mat4& world, const std::vector<std::uint8_t>& remap = {})
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
        if (!remap.empty())
            readSkin(prim, remap, mesh);

        if (prim.indices) {
            mesh.indices.reserve(prim.indices->count);
            for (cgltf_size i = 0; i < prim.indices->count; ++i)
                mesh.indices.push_back(static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, i)));
        } else {
            for (cgltf_size i = 0; i < positions->count; ++i)
                mesh.indices.push_back(static_cast<std::uint32_t>(i));
        }

        if (!normals)
            generateNormals(mesh);
        mesh.material = materialFor(prim.material);
        m_model.meshes.push_back(std::move(mesh));
    }

    // Area-weighted smooth normals for primitives that come without any
    static void generateNormals(MeshData& mesh)
    {
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            Vertex& a = mesh.vertices[mesh.indices[i]];
            Vertex& b = mesh.vertices[mesh.indices[i + 1]];
            Vertex& c = mesh.vertices[mesh.indices[i + 2]];
            const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
            a.normal += n;
            b.normal += n;
            c.normal += n;
        }
        for (Vertex& v : mesh.vertices)
            v.normal = glm::length(v.normal) > 1e-12f ? glm::normalize(v.normal) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // A mesh that moves with one joint
    void addRigidPrimitive(const cgltf_primitive& prim, const glm::mat4& toJoint, std::uint8_t joint)
    {
        const std::size_t before = m_model.meshes.size();
        addPrimitive(prim, toJoint);
        if (m_model.meshes.size() == before)
            return;
        MeshData& mesh = m_model.meshes.back();
        mesh.joints.assign(mesh.vertices.size(), glm::u8vec4(joint, 0, 0, 0));
        mesh.weights.assign(mesh.vertices.size(), glm::u8vec4(255, 0, 0, 0));
    }

    static void readSkin(const cgltf_primitive& prim, const std::vector<std::uint8_t>& remap, MeshData& mesh)
    {
        const cgltf_accessor* joints[2] = {findAttribute(prim, cgltf_attribute_type_joints, 0),
                                           findAttribute(prim, cgltf_attribute_type_joints, 1)};
        const cgltf_accessor* weights[2] = {findAttribute(prim, cgltf_attribute_type_weights, 0),
                                            findAttribute(prim, cgltf_attribute_type_weights, 1)};
        const std::size_t count = mesh.vertices.size();
        mesh.joints.resize(count);
        mesh.weights.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            cgltf_uint js[8] = {};
            float ws[8] = {};
            int n = 0;
            for (int set = 0; set < 2; ++set) {
                if (!joints[set] || !weights[set])
                    continue;
                cgltf_accessor_read_uint(joints[set], i, js + n, 4);
                cgltf_accessor_read_float(weights[set], i, ws + n, 4);
                n += 4;
            }
            quantizeInfluences(js, ws, n, remap, mesh.joints[i], mesh.weights[i]);
        }
    }

    void setAnimation(std::optional<SkeletonData> skeleton, std::vector<ClipData> clips)
    {
        m_model.skeleton = std::move(skeleton);
        m_model.clips = std::move(clips);
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

    std::optional<SkeletonBuild> skeleton = buildSkeleton(data, path);
    std::vector<std::vector<std::uint8_t>> skinRemaps(data->skins_count);
    if (skeleton) {
        for (cgltf_size k = 0; k < data->skins_count; ++k) {
            const cgltf_skin& skin = data->skins[k];
            for (cgltf_size j = 0; j < skin.joints_count; ++j)
                skinRemaps[k].push_back(static_cast<std::uint8_t>(
                    skeleton->nodeJoint[static_cast<std::size_t>(skin.joints[j] - data->nodes)]));
        }
    }

    GltfBuilder builder(data, path);
    bool anySkinned = false;
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh)
            continue;
        // glTF ignores a skinned mesh node's own transform: the joints place it
        if (skeleton && node.skin && node.skin->joints_count > 0) {
            const std::vector<std::uint8_t>& remap = skinRemaps[static_cast<std::size_t>(node.skin - data->skins)];
            for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
                builder.addPrimitive(node.mesh->primitives[p], glm::mat4(1.0f), remap);
            anySkinned = true;
            continue;
        }
        float world[16];
        cgltf_node_transform_world(&node, world);
        const int rigidJoint = skeleton ? skeleton->nodeJoint[n] : -1;
        if (rigidJoint >= 0) {
            // Stored so that the joint's palette entry (model * inverseBind) puts it back where it was
            const SkeletonData& s = skeleton->skeleton;
            const auto j = static_cast<std::size_t>(rigidJoint);
            const glm::mat4 toJoint = glm::inverse(s.inverseBind[j].toMat4()) * glm::inverse(s.bindModel[j].toMat4()) *
                                      glm::make_mat4(world);
            const std::vector<std::uint8_t> remap{static_cast<std::uint8_t>(rigidJoint)};
            for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
                builder.addRigidPrimitive(node.mesh->primitives[p], toJoint, remap.front());
            anySkinned = true;
            continue;
        }
        for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
            builder.addPrimitive(node.mesh->primitives[p], glm::make_mat4(world));
    }

    if (skeleton && anySkinned) {
        std::vector<ClipData> clips;
        std::set<std::string> names;
        for (cgltf_size a = 0; a < data->animations_count; ++a) {
            const cgltf_animation& animation = data->animations[a];
            std::string base = animation.name && *animation.name ? std::string(animation.name) : "Clip " + std::to_string(a);
            std::replace(base.begin(), base.end(), '#', '_');  // separates the model from the clip in references
            std::string name = base;
            for (int i = 2; names.contains(name); ++i)
                name = base + " " + std::to_string(i);
            names.insert(name);
            clips.push_back(buildClip(data, animation, *skeleton, name, path));
        }
        builder.setAnimation(std::move(skeleton->skeleton), std::move(clips));
    }
    cgltf_free(data);

    ModelData model = builder.take();
    if (model.meshes.empty()) {
        std::fprintf(stderr, "No triangle meshes in %s\n", path.c_str());
        return std::nullopt;
    }
    return model;
}
