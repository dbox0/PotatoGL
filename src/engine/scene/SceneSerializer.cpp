#include "SceneSerializer.h"

#include "engine/reflect/ComponentRegistry.h"
#include "engine/scene/Scene.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

using json = nlohmann::ordered_json;  // keeps "uuid", "parent", "components" in that order

static constexpr int SceneVersion = 2;

// Shortest decimal that round-trips the float, so 0.8f is written as 0.8, not 0.800000011920929
static double jsonFloat(float f)
{
    if (!std::isfinite(f))
        return 0.0;
    char buffer[32];
    const auto end = std::to_chars(buffer, buffer + sizeof(buffer), f).ptr;
    double d = 0.0;
    std::from_chars(buffer, end, d);
    return d;
}

// Hex strings: JSON numbers lose precision past 2^53
static std::string uuidText(std::uint64_t uuid)
{
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016" PRIx64, uuid);
    return buffer;
}

static std::uint64_t parseUuid(const json& j)
{
    if (!j.is_string())
        return 0;
    const std::string& text = j.get_ref<const std::string&>();
    std::uint64_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return value;
}

static json entityRef(const Scene& scene, entt::entity e)
{
    const std::uint64_t uuid = e == entt::null || !scene.registry().valid(e) ? 0 : scene.uuidOf(e);
    return uuid == 0 ? json(nullptr) : json(uuidText(uuid));
}

static json toJson(const FieldValue& value, const Scene& scene)
{
    return std::visit(
        [&](const auto& v) -> json {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, float>)
                return jsonFloat(v);
            else if constexpr (std::is_same_v<T, glm::vec3>)
                return json::array({jsonFloat(v.x), jsonFloat(v.y), jsonFloat(v.z)});
            else if constexpr (std::is_same_v<T, glm::quat>)
                return json::array({jsonFloat(v.x), jsonFloat(v.y), jsonFloat(v.z), jsonFloat(v.w)});
            else if constexpr (std::is_same_v<T, entt::entity>)
                return entityRef(scene, v);
            else
                return v;
        },
        value);
}

static bool floatsFrom(const json& j, float* out, std::size_t count)
{
    if (!j.is_array() || j.size() != count)
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!j[i].is_number())
            return false;
        out[i] = j[i].get<float>();
    }
    return true;
}

// Every type except Entity, which is resolved once all entities of a load exist
static bool fromJson(FieldType type, const json& j, FieldValue& out)
{
    switch (type) {
    case FieldType::Float:
        if (!j.is_number())
            return false;
        out = j.get<float>();
        return true;
    case FieldType::Int:
        if (!j.is_number_integer())
            return false;
        out = j.get<int>();
        return true;
    case FieldType::Bool:
        if (!j.is_boolean())
            return false;
        out = j.get<bool>();
        return true;
    case FieldType::String:
        if (!j.is_string())
            return false;
        out = j.get<std::string>();
        return true;
    case FieldType::Vec3: {
        glm::vec3 v;
        if (!floatsFrom(j, &v.x, 3))
            return false;
        out = v;
        return true;
    }
    case FieldType::Quat: {
        float q[4];
        if (!floatsFrom(j, q, 4))
            return false;
        out = glm::quat(q[3], q[0], q[1], q[2]);
        return true;
    }
    case FieldType::Entity:
        return false;
    }
    return false;
}

static void writePretty(std::string& out, const json& j, int indent)
{
    const auto newline = [&](int n) {
        out += '\n';
        out.append(static_cast<std::size_t>(n), ' ');
    };
    if (j.is_object() && !j.empty()) {
        out += '{';
        bool first = true;
        for (const auto& [key, value] : j.items()) {
            if (!first)
                out += ',';
            first = false;
            newline(indent + 2);
            out += json(key).dump();
            out += ": ";
            writePretty(out, value, indent + 2);
        }
        newline(indent);
        out += '}';
    } else if (j.is_array() && !j.empty()) {
        bool flat = true;
        for (const json& element : j)
            flat = flat && !element.is_structured();
        out += '[';
        for (std::size_t i = 0; i < j.size(); ++i) {
            if (i > 0)
                out += flat ? ", " : ",";
            if (!flat)
                newline(indent + 2);
            writePretty(out, j[i], indent + 2);
        }
        if (!flat)
            newline(indent);
        out += ']';
    } else {
        out += j.dump();
    }
}


static json componentJson(const ComponentInfo& info, Scene& scene, entt::entity e)
{
    json jsonComponent = json::object();
    if (const void* data = info.get(scene.registry(), e)) {
        for (const FieldInfo& field : info.fields) {
            if (field.saved && field.set)
                jsonComponent[field.key] = toJson(field.get(data), scene);
        }
    }
    return jsonComponent;
}

static json entityJson(Scene& scene, const ComponentRegistry& components, entt::entity e, bool withParent)
{
    entt::registry& registry = scene.registry();
    json jsonEntity = json::object();
    jsonEntity["uuid"] = uuidText(scene.uuidOf(e));
    const entt::entity parent = scene.parentOf(e);
    if (withParent && parent != scene.root())
        jsonEntity["parent"] = uuidText(scene.uuidOf(parent));

    json jsonComponents = json::object();
    for (const auto& info : components.all()) {
        if (info->has(registry, e))
            jsonComponents[info->id] = componentJson(*info, scene, e);
    }
    jsonEntity["components"] = std::move(jsonComponents);
    return jsonEntity;
}

static void collectTree(const Scene& scene, entt::entity e, std::vector<entt::entity>& out)
{
    out.push_back(e);
    scene.forEachChild(e, [&](entt::entity child) { collectTree(scene, child, out); });
}

static std::string writeDocument(json entities)
{
    json root = json::object();
    root["version"] = SceneVersion;
    root["entities"] = std::move(entities);
    std::string text;
    writePretty(text, root, 0);
    return text;
}

std::string saveScene(Scene& scene, const ComponentRegistry& components, std::vector<entt::entity>* order)
{
    std::vector<entt::entity> entities;
    scene.forEachChild(scene.root(), [&](entt::entity e) { collectTree(scene, e, entities); });

    json jsonEntities = json::array();
    for (const entt::entity e : entities)
        jsonEntities.push_back(entityJson(scene, components, e, true));
    if (order)
        *order = std::move(entities);
    return writeDocument(std::move(jsonEntities));
}

std::string saveEntityTree(Scene& scene, const ComponentRegistry& components, entt::entity root)
{
    std::vector<entt::entity> entities;
    collectTree(scene, root, entities);
    json jsonEntities = json::array();
    for (const entt::entity e : entities)
        jsonEntities.push_back(entityJson(scene, components, e, e != root));
    return writeDocument(std::move(jsonEntities));
}

std::string saveComponent(const ComponentInfo& info, Scene& scene, entt::entity e)
{
    return componentJson(info, scene, e).dump();
}

namespace
{
struct PendingEntityField
{
    entt::entity entity;
    const ComponentInfo* info;
    std::size_t field;
    json ref;
};

// Maps saved references to created entities. In-file uuids win over the scene's, since a
// colliding saved uuid gets a fresh one on load.
struct RefMap
{
    std::unordered_map<std::uint64_t, entt::entity> byUuid;
    std::unordered_map<int, entt::entity> byFileId;  // version 1 files

    entt::entity resolve(const Scene& scene, const json& ref) const
    {
        if (ref.is_number_integer()) {
            const auto it = byFileId.find(ref.get<int>());
            return it == byFileId.end() ? entt::null : it->second;
        }
        const std::uint64_t uuid = parseUuid(ref);
        if (uuid == 0)
            return entt::null;
        if (const auto it = byUuid.find(uuid); it != byUuid.end())
            return it->second;
        const entt::entity e = scene.findByUuid(uuid);
        return e == scene.root() ? entt::null : e;
    }
};
}  // namespace

static void applyFields(const ComponentInfo& info, void* data, const json& jsonComponent, entt::entity e,
                        std::vector<PendingEntityField>& pending)
{
    for (std::size_t i = 0; i < info.fields.size(); ++i) {
        const FieldInfo& field = info.fields[i];
        if (!field.saved || !field.set)
            continue;
        const auto it = jsonComponent.find(field.key);
        if (it == jsonComponent.end())
            continue;
        if (field.type == FieldType::Entity) {
            pending.push_back({e, &info, i, *it});
            continue;
        }
        FieldValue value;
        if (fromJson(field.type, *it, value))
            field.set(data, value);
        else
            std::fprintf(stderr, "Scene: bad value for %s.%s\n", info.id.c_str(), field.key.c_str());
    }
}

static void addOrApply(const ComponentInfo& info, entt::registry& registry, entt::entity e, const json& jsonComponent,
                       std::vector<PendingEntityField>& pending)
{
    if (info.has(registry, e)) {
        if (void* data = info.get(registry, e))
            applyFields(info, data, jsonComponent, e, pending);
    } else if (info.addWith) {
        info.addWith(registry, e, [&](void* data) { applyFields(info, data, jsonComponent, e, pending); });
    }
}

static void resolvePending(Scene& scene, const RefMap& refs, const std::vector<PendingEntityField>& pending)
{
    entt::registry& registry = scene.registry();
    for (const PendingEntityField& p : pending) {
        if (!registry.valid(p.entity) || !p.info->has(registry, p.entity))
            continue;
        p.info->fields[p.field].set(p.info->get(registry, p.entity), refs.resolve(scene, p.ref));
    }
}

// Creates the entities of a document. Ones whose parent isn't in the document go under
// parent, starting at index. Returns the created entities in document order.
static std::vector<entt::entity> instantiate(Scene& scene, const ComponentRegistry& components,
                                             const json& jsonEntities, entt::entity parent, std::size_t index)
{
    entt::registry& registry = scene.registry();
    RefMap refs;
    std::vector<PendingEntityField> pending;
    std::vector<entt::entity> created;

    for (const json& jsonEntity : jsonEntities) {
        if (!jsonEntity.is_object())
            continue;

        entt::entity entityParent = entt::null;
        if (const auto p = jsonEntity.find("parent"); p != jsonEntity.end())
            entityParent = refs.resolve(scene, *p);
        std::size_t entityIndex = SIZE_MAX;
        if (entityParent == entt::null) {
            entityParent = parent;
            entityIndex = index;
            if (index != SIZE_MAX)
                ++index;
        }

        const auto savedUuid = jsonEntity.find("uuid");
        const std::uint64_t uuid = savedUuid == jsonEntity.end() ? 0 : parseUuid(*savedUuid);
        const entt::entity e = scene.createGameObject("", entityParent, uuid, entityIndex);
        created.push_back(e);
        if (uuid != 0)
            refs.byUuid[uuid] = e;
        if (const auto id = jsonEntity.find("id"); id != jsonEntity.end() && id->is_number_integer())
            refs.byFileId[id->get<int>()] = e;

        const auto jsonComponents = jsonEntity.find("components");
        if (jsonComponents == jsonEntity.end() || !jsonComponents->is_object())
            continue;
        for (const auto& [key, jsonComponent] : jsonComponents->items()) {
            const ComponentInfo* info = components.find(key);
            if (!info) {
                std::fprintf(stderr, "Scene: unknown component '%s' skipped\n", key.c_str());
                continue;
            }
            if (jsonComponent.is_object())
                addOrApply(*info, registry, e, jsonComponent, pending);
        }
    }

    resolvePending(scene, refs, pending);
    return created;
}

static const json* findEntities(const json& root, std::string* error)
{
    const auto fail = [&](const char* message) -> const json* {
        if (error)
            *error = message;
        return nullptr;
    };
    if (root.is_discarded() || !root.is_object())
        return fail("not a JSON object");
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_integer())
        return fail("missing version");
    if (version->get<int>() > SceneVersion)
        return fail("scene was saved by a newer version");
    const auto entities = root.find("entities");
    if (entities == root.end() || !entities->is_array())
        return fail("missing entities array");
    return &*entities;
}

bool loadScene(Scene& scene, const ComponentRegistry& components, std::string_view text, std::string* error,
               std::vector<entt::entity>* order)
{
    const json root = json::parse(text, nullptr, false);
    const json* entities = findEntities(root, error);
    if (!entities)
        return false;
    scene.clear();
    std::vector<entt::entity> created = instantiate(scene, components, *entities, entt::null, SIZE_MAX);
    if (order)
        *order = std::move(created);
    return true;
}

entt::entity loadEntityTree(Scene& scene, const ComponentRegistry& components, std::string_view text,
                            entt::entity parent, std::size_t index)
{
    const json root = json::parse(text, nullptr, false);
    const json* entities = findEntities(root, nullptr);
    if (!entities)
        return entt::null;
    const std::vector<entt::entity> created = instantiate(scene, components, *entities, parent, index);
    return created.empty() ? entt::null : created.front();
}

bool loadComponent(const ComponentInfo& info, Scene& scene, entt::entity e, std::string_view text)
{
    const json jsonComponent = json::parse(text, nullptr, false);
    if (jsonComponent.is_discarded() || !jsonComponent.is_object())
        return false;
    std::vector<PendingEntityField> pending;
    addOrApply(info, scene.registry(), e, jsonComponent, pending);
    resolvePending(scene, RefMap{}, pending);
    return info.has(scene.registry(), e);
}

bool saveSceneFile(Scene& scene, const ComponentRegistry& components, const std::string& path, std::string* error)
{
    std::error_code ec;
    if (const std::filesystem::path parent = std::filesystem::path(path).parent_path(); !parent.empty())
        std::filesystem::create_directories(parent, ec);
    std::ofstream file(path);
    if (file)
        file << saveScene(scene, components) << '\n';
    if (!file) {
        if (error)
            *error = "can't write " + path;
        return false;
    }
    scene.setPath(path);
    return true;
}

bool loadSceneFile(Scene& scene, const ComponentRegistry& components, const std::string& path, std::string* error)
{
    std::ifstream file(path);
    if (!file) {
        if (error)
            *error = "can't open " + path;
        return false;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    if (!loadScene(scene, components, ss.str(), error))
        return false;
    scene.setPath(path);
    return true;
}
