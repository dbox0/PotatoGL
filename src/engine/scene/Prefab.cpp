#include "Prefab.h"

#include "engine/core/Paths.h"
#include "engine/reflect/ComponentRegistry.h"
#include "engine/scene/Scene.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/scene/Transform.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
std::uint64_t splitmix(std::uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

std::uint64_t derivedUuid(std::uint64_t root, std::uint64_t saved)
{
    const std::uint64_t uuid = splitmix(root ^ splitmix(saved));
    return uuid == 0 ? 1 : uuid;
}

std::uint64_t randomUuid()
{
    static std::mt19937_64 rng{std::random_device{}()};
    std::uint64_t uuid = 0;
    while (uuid == 0)
        uuid = rng();
    return uuid;
}

std::optional<std::string> readPrefab(const std::string& ref)
{
    std::ifstream file(resourcePath(ref));
    if (!file)
        return std::nullopt;
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void collectTree(const Scene& scene, entt::entity e, std::vector<entt::entity>& out)
{
    out.push_back(e);
    scene.forEachChild(e, [&](entt::entity child) { collectTree(scene, child, out); });
}

entt::entity instantiate(Scene& scene, const ComponentRegistry& components, const std::string& ref,
                         entt::entity parent, std::size_t index, std::uint64_t rootUuid,
                         std::vector<std::string>& chain)
{
    // A prefab inside itself keeps the copy it was saved with
    if (std::find(chain.begin(), chain.end(), ref) != chain.end())
        return entt::null;
    const auto text = readPrefab(ref);
    if (!text)
        return entt::null;
    const std::uint64_t root = rootUuid != 0 ? rootUuid : randomUuid();
    bool first = true;
    const entt::entity e = loadEntityTree(scene, components, *text, parent, index, [&](std::uint64_t saved) {
        if (std::exchange(first, false))
            return root;
        return saved == 0 ? std::uint64_t(0) : derivedUuid(root, saved);
    });
    if (e == entt::null)
        return entt::null;
    entt::registry& registry = scene.registry();
    registry.emplace_or_replace<PrefabInstance>(e, ref);

    // Nested instances come from their own files, which may be newer than this one's copy
    chain.push_back(ref);
    std::vector<entt::entity> tree;
    collectTree(scene, e, tree);
    for (std::size_t i = 1; i < tree.size(); ++i) {
        const entt::entity nested = tree[i];
        if (!registry.valid(nested))
            continue;  // inside a nested instance rebuilt already
        const PrefabInstance* instance = registry.try_get<PrefabInstance>(nested);
        if (!instance)
            continue;
        const std::string nestedRef = instance->prefab;
        const entt::entity nestedParent = scene.parentOf(nested);
        const std::size_t nestedIndex = scene.siblingIndex(nested);
        const std::uint64_t uuid = scene.uuidOf(nested);
        const Transform transform = registry.get<Transform>(nested);
        const std::string name = registry.get<Name>(nested).value;
        if (!readPrefab(nestedRef) || std::find(chain.begin(), chain.end(), nestedRef) != chain.end())
            continue;
        scene.destroyRecursive(nested);
        const entt::entity rebuilt =
            instantiate(scene, components, nestedRef, nestedParent, nestedIndex, uuid, chain);
        if (rebuilt == entt::null)
            continue;
        registry.get<Transform>(rebuilt) = transform;
        registry.get<Name>(rebuilt).value = name;
    }
    chain.pop_back();
    return e;
}
}  // namespace

entt::entity instantiatePrefab(Scene& scene, const ComponentRegistry& components, const std::string& ref,
                               entt::entity parent, std::size_t index, std::uint64_t rootUuid)
{
    std::vector<std::string> chain;
    return instantiate(scene, components, ref, parent, index, rootUuid, chain);
}

int refreshPrefabInstances(Scene& scene, const ComponentRegistry& components, const std::string& ref)
{
    entt::registry& registry = scene.registry();

    // Outermost instances only: rebuilding one rebuilds the instances nested in it
    std::vector<entt::entity> instances;
    std::unordered_set<entt::entity> matching;
    for (auto [e, instance] : registry.view<PrefabInstance>().each()) {
        if (ref.empty() || instance.prefab == ref)
            matching.insert(e);
    }
    for (const entt::entity e : matching) {
        bool nested = false;
        for (entt::entity p = scene.parentOf(e); p != entt::null && p != scene.root() && !nested;
             p = scene.parentOf(p))
            nested = matching.contains(p);
        if (!nested)
            instances.push_back(e);
    }

    std::unordered_map<entt::entity, std::uint64_t> replaced;  // destroyed objects' uuids
    std::unordered_map<std::string, bool> readable;
    int changed = 0;
    for (const entt::entity e : instances) {
        const std::string prefab = registry.get<PrefabInstance>(e).prefab;
        auto [it, inserted] = readable.try_emplace(prefab, false);
        if (inserted)
            it->second = readPrefab(prefab).has_value();
        if (!it->second)
            continue;

        const std::string before = saveEntityTree(scene, components, e);
        const entt::entity parent = scene.parentOf(e);
        const std::size_t index = scene.siblingIndex(e);
        const std::uint64_t uuid = scene.uuidOf(e);
        const Transform transform = registry.get<Transform>(e);
        const std::string name = registry.get<Name>(e).value;
        std::vector<entt::entity> tree;
        collectTree(scene, e, tree);
        for (const entt::entity old : tree)
            replaced[old] = scene.uuidOf(old);

        scene.destroyRecursive(e);
        const entt::entity rebuilt = instantiatePrefab(scene, components, prefab, parent, index, uuid);
        if (rebuilt == entt::null)
            continue;
        registry.get<Transform>(rebuilt) = transform;
        registry.get<Name>(rebuilt).value = name;
        if (saveEntityTree(scene, components, rebuilt) != before)
            ++changed;
    }
    if (replaced.empty())
        return changed;

    // Entity fields elsewhere held handles to the old objects
    for (const auto& info : components.all()) {
        for (const FieldInfo& field : info->fields) {
            if (field.type != FieldType::Entity || !field.set)
                continue;
            for (const entt::entity e : registry.view<Uuid>()) {
                if (!info->has(registry, e))
                    continue;
                void* data = info->get(registry, e);
                const auto target = std::get<entt::entity>(field.get(data));
                if (target == entt::null || registry.valid(target))
                    continue;
                if (const auto old = replaced.find(target); old != replaced.end())
                    field.set(data, scene.findByUuid(old->second));
            }
        }
    }
    return changed;
}
