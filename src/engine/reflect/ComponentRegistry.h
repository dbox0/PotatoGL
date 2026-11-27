#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

// Minimal runtime reflection for tools: the inspector edits through it, the scene
// serializer saves and loads through it. No UI or file format code here.
//
// Keys (component ids, field keys) are what scene files store, so renaming one breaks
// existing scenes. Labels are only for display and default to a nicified key.

enum class FieldType
{
    Float,
    Int,
    Bool,
    Vec3,
    Quat,
    String,
    Entity,
};

using FieldValue = std::variant<float, int, bool, glm::vec3, glm::quat, std::string, entt::entity>;

struct FieldInfo
{
    std::string key;
    std::string label;
    FieldType type;
    FieldValue (*get)(const void* component);
    void (*set)(void* component, const FieldValue& value);  // nullptr = read-only, never saved
    float speed;            // drag speed for numeric widgets
    bool saved = true;      // written to scene files
    bool visible = true;    // shown in the inspector
};

// "radiansPerSec" -> "Radians Per Sec", "MeshRenderer" -> "Mesh Renderer"
inline std::string nicifyName(std::string_view key)
{
    std::string out;
    for (std::size_t i = 0; i < key.size(); ++i) {
        const char c = key[i];
        const bool upper = c >= 'A' && c <= 'Z';
        const bool prevLower = i > 0 && key[i - 1] >= 'a' && key[i - 1] <= 'z';
        if (upper && prevLower)
            out += ' ';
        out += (i == 0 && c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    return out;
}

namespace detail
{
template<typename>
struct MemberTraits;

template<typename Class, typename Value>
struct MemberTraits<Value Class::*>
{
    using ClassType = Class;
    using ValueType = Value;
};

template<typename T>
constexpr FieldType fieldTypeOf()
{
    if constexpr (std::is_same_v<T, float>)
        return FieldType::Float;
    else if constexpr (std::is_same_v<T, int>)
        return FieldType::Int;
    else if constexpr (std::is_same_v<T, bool>)
        return FieldType::Bool;
    else if constexpr (std::is_same_v<T, glm::vec3>)
        return FieldType::Vec3;
    else if constexpr (std::is_same_v<T, glm::quat>)
        return FieldType::Quat;
    else if constexpr (std::is_same_v<T, std::string>)
        return FieldType::String;
    else if constexpr (std::is_same_v<T, entt::entity>)
        return FieldType::Entity;
    else
        static_assert(!sizeof(T*), "unsupported field type");
}
}  // namespace detail

struct ComponentInfo
{
    std::string id;    // scene file key
    std::string name;  // display name
    bool isScript = false;
    bool addable = true;
    bool removable = true;
    bool (*has)(entt::registry&, entt::entity) = nullptr;
    void* (*get)(entt::registry&, entt::entity) = nullptr;  // nullptr result for tags
    void (*add)(entt::registry&, entt::entity) = nullptr;
    // Builds the component, lets init fill it, then emplaces it, so construction hooks
    // (scripts' onCreate) already see the values. nullptr if not default constructible.
    void (*addWith)(entt::registry&, entt::entity, const std::function<void(void*)>& init) = nullptr;
    void (*remove)(entt::registry&, entt::entity) = nullptr;
    std::vector<FieldInfo> fields;

    template<auto Member>
    ComponentInfo& field(const char* key, const char* label, float speed = 0.1f)
    {
        using Class = typename detail::MemberTraits<decltype(Member)>::ClassType;
        using Value = typename detail::MemberTraits<decltype(Member)>::ValueType;
        fields.push_back({key, label, detail::fieldTypeOf<Value>(),
                          [](const void* c) -> FieldValue { return static_cast<const Class*>(c)->*Member; },
                          [](void* c, const FieldValue& v) { static_cast<Class*>(c)->*Member = std::get<Value>(v); },
                          speed});
        return *this;
    }

    // key should be the member's name
    template<auto Member>
    ComponentInfo& field(const char* key, float speed = 0.1f)
    {
        return field<Member>(key, nicifyName(key).c_str(), speed);
    }

    ComponentInfo& property(const char* key, const char* label, FieldType type, FieldValue (*getter)(const void*),
                            void (*setter)(void*, const FieldValue&), float speed = 0.1f)
    {
        fields.push_back({key, label, type, getter, setter, speed});
        return *this;
    }

    // Modifiers for the field added last
    ComponentInfo& notSaved()
    {
        fields.back().saved = false;
        return *this;
    }
    ComponentInfo& hidden()
    {
        fields.back().visible = false;
        return *this;
    }

    ComponentInfo& label(std::string displayName)
    {
        name = std::move(displayName);
        return *this;
    }

    // Always present, can't be added or removed from tools
    ComponentInfo& locked()
    {
        addable = false;
        removable = false;
        return *this;
    }
};

class ComponentRegistry
{
private:
    std::vector<std::unique_ptr<ComponentInfo>> m_infos;  // stable addresses

public:
    template<typename T>
    ComponentInfo& add(std::string id)
    {
        auto info = std::make_unique<ComponentInfo>();
        info->name = nicifyName(id);
        info->id = std::move(id);
        info->has = [](entt::registry& r, entt::entity e) { return r.all_of<T>(e); };
        info->get = [](entt::registry& r, entt::entity e) -> void* {
            if constexpr (std::is_empty_v<T>)
                return nullptr;
            else
                return &r.get<T>(e);
        };
        if constexpr (std::is_default_constructible_v<T>) {
            info->add = [](entt::registry& r, entt::entity e) { r.emplace<T>(e); };
            info->addWith = [](entt::registry& r, entt::entity e, const std::function<void(void*)>& init) {
                if constexpr (std::is_empty_v<T>) {
                    r.emplace<T>(e);
                } else {
                    T value{};
                    init(&value);
                    r.emplace<T>(e, std::move(value));
                }
            };
        } else {
            info->addable = false;
        }
        info->remove = [](entt::registry& r, entt::entity e) { r.remove<T>(e); };
        m_infos.push_back(std::move(info));
        return *m_infos.back();
    }

    const ComponentInfo* find(std::string_view id) const
    {
        for (const auto& info : m_infos) {
            if (info->id == id)
                return info.get();
        }
        return nullptr;
    }

    const std::vector<std::unique_ptr<ComponentInfo>>& all() const { return m_infos; }
};
