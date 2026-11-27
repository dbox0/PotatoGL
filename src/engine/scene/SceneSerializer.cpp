#include "SceneSerializer.h"

#include "engine/reflect/ComponentRegistry.h"
#include "engine/scene/Scene.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

using json = nlohmann::ordered_json;  // keeps "id", "parent", "components" in that order

static constexpr int SceneVersion = 1;

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

static json toJson(const FieldValue& value, const std::unordered_map<entt::entity, int>& fileIds)
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
            else if constexpr (std::is_same_v<T, entt::entity>) {
                const auto it = fileIds.find(v);
                return it == fileIds.end() ? json(nullptr) : json(it->second);
            } else
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

// Every type except Entity, which needs the id map and is resolved after all entities exist
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

// Like dump(2), but arrays of plain values stay on one line: "position": [0.0, 1.5, 0.0]
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

std::string saveScene(Scene& scene, const ComponentRegistry& components, std::vector<entt::entity>* order)
{
    entt::registry& registry = scene.registry();

    std::vector<entt::entity> entities;
    const auto collect = [&](auto&& self, entt::entity e) -> void {
        entities.push_back(e);
        scene.forEachChild(e, [&](entt::entity child) { self(self, child); });
    };
    scene.forEachChild(scene.root(), [&](entt::entity e) { collect(collect, e); });

    std::unordered_map<entt::entity, int> fileIds;
    for (std::size_t i = 0; i < entities.size(); ++i)
        fileIds.emplace(entities[i], static_cast<int>(i) + 1);

    json jsonEntities = json::array();
    for (const entt::entity e : entities) {
        json jsonEntity;
        jsonEntity["id"] = fileIds.at(e);
        const entt::entity parent = registry.get<Relationship>(e).parent;
        if (parent != scene.root())
            jsonEntity["parent"] = fileIds.at(parent);

        json jsonComponents = json::object();
        for (const auto& info : components.all()) {
            if (!info->has(registry, e))
                continue;
            json jsonComponent = json::object();
            if (const void* data = info->get(registry, e)) {
                for (const FieldInfo& field : info->fields) {
                    if (field.saved && field.set)
                        jsonComponent[field.key] = toJson(field.get(data), fileIds);
                }
            }
            jsonComponents[info->id] = std::move(jsonComponent);
        }
        jsonEntity["components"] = std::move(jsonComponents);
        jsonEntities.push_back(std::move(jsonEntity));
    }

    if (order)
        *order = std::move(entities);
    json root = json::object();
    root["version"] = SceneVersion;
    root["entities"] = std::move(jsonEntities);
    std::string text;
    writePretty(text, root, 0);
    return text;
}

namespace
{
struct PendingEntityField
{
    entt::entity entity;
    const ComponentInfo* info;
    std::size_t field;
    int fileId;
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
            if (it->is_number_integer())
                pending.push_back({e, &info, i, it->get<int>()});
            continue;
        }
        FieldValue value;
        if (fromJson(field.type, *it, value))
            field.set(data, value);
        else
            std::fprintf(stderr, "Scene: bad value for %s.%s\n", info.id.c_str(), field.key.c_str());
    }
}

bool loadScene(Scene& scene, const ComponentRegistry& components, std::string_view text, std::string* error,
               std::vector<entt::entity>* order)
{
    const auto fail = [&](const char* message) {
        if (error)
            *error = message;
        return false;
    };

    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object())
        return fail("not a JSON object");
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_integer())
        return fail("missing version");
    if (version->get<int>() > SceneVersion)
        return fail("scene was saved by a newer version");
    const auto jsonEntities = root.find("entities");
    if (jsonEntities == root.end() || !jsonEntities->is_array())
        return fail("missing entities array");

    scene.clear();
    entt::registry& registry = scene.registry();
    std::unordered_map<int, entt::entity> byId;
    std::vector<PendingEntityField> pending;
    if (order)
        order->clear();

    for (const json& jsonEntity : *jsonEntities) {
        const auto id = jsonEntity.find("id");
        if (!jsonEntity.is_object() || id == jsonEntity.end() || !id->is_number_integer()) {
            std::fprintf(stderr, "Scene: skipped an entity without an id\n");
            continue;
        }

        entt::entity parent = entt::null;
        if (const auto p = jsonEntity.find("parent"); p != jsonEntity.end() && p->is_number_integer()) {
            if (const auto it = byId.find(p->get<int>()); it != byId.end())
                parent = it->second;
            else
                std::fprintf(stderr, "Scene: parent %d of %d not found, moved to the top level\n", p->get<int>(),
                             id->get<int>());
        }

        const entt::entity e = scene.createGameObject("", parent);
        byId[id->get<int>()] = e;
        if (order)
            order->push_back(e);

        const auto jsonComponents = jsonEntity.find("components");
        if (jsonComponents == jsonEntity.end() || !jsonComponents->is_object())
            continue;
        for (const auto& [key, jsonComponent] : jsonComponents->items()) {
            const ComponentInfo* info = components.find(key);
            if (!info) {
                std::fprintf(stderr, "Scene: unknown component '%s' skipped\n", key.c_str());
                continue;
            }
            if (!jsonComponent.is_object())
                continue;
            if (info->has(registry, e)) {
                if (void* data = info->get(registry, e))
                    applyFields(*info, data, jsonComponent, e, pending);
            } else if (info->addWith) {
                info->addWith(registry, e, [&](void* data) { applyFields(*info, data, jsonComponent, e, pending); });
            }
        }
    }

    for (const PendingEntityField& p : pending) {
        if (!registry.valid(p.entity) || !p.info->has(registry, p.entity))
            continue;
        const auto it = byId.find(p.fileId);
        p.info->fields[p.field].set(p.info->get(registry, p.entity), it == byId.end() ? entt::null : it->second);
    }
    return true;
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
