#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

// Minimal runtime reflection so tools (the editor inspector) can list and edit
// components without knowing their types. No UI code here.

enum class FieldType
{
    Float,
    Int,
    Bool,
    Vec3,
    String,
    Entity,
};

using FieldValue = std::variant<float, int, bool, glm::vec3, std::string, entt::entity>;

struct FieldInfo
{
    const char* name;
    FieldType type;
    FieldValue (*get)(const void* component);
    void (*set)(void* component, const FieldValue& value);  // nullptr = read-only
    float speed;  // drag speed for numeric widgets
};

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
    std::string name;
    bool isScript = false;
    bool addable = true;
    bool removable = true;
    bool (*has)(entt::registry&, entt::entity) = nullptr;
    void* (*get)(entt::registry&, entt::entity) = nullptr;  // nullptr result for tags
    void (*add)(entt::registry&, entt::entity) = nullptr;
    void (*remove)(entt::registry&, entt::entity) = nullptr;
    std::vector<FieldInfo> fields;

    template<auto Member>
    ComponentInfo& field(const char* label, float speed = 0.1f)
    {
        using Class = typename detail::MemberTraits<decltype(Member)>::ClassType;
        using Value = typename detail::MemberTraits<decltype(Member)>::ValueType;
        fields.push_back({label, detail::fieldTypeOf<Value>(),
                          [](const void* c) -> FieldValue { return static_cast<const Class*>(c)->*Member; },
                          [](void* c, const FieldValue& v) { static_cast<Class*>(c)->*Member = std::get<Value>(v); },
                          speed});
        return *this;
    }

    ComponentInfo& property(const char* label, FieldType type, FieldValue (*getter)(const void*),
                            void (*setter)(void*, const FieldValue&), float speed = 0.1f)
    {
        fields.push_back({label, type, getter, setter, speed});
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
    ComponentInfo& add(std::string name)
    {
        auto info = std::make_unique<ComponentInfo>();
        info->name = std::move(name);
        info->has = [](entt::registry& r, entt::entity e) { return r.all_of<T>(e); };
        info->get = [](entt::registry& r, entt::entity e) -> void* {
            if constexpr (std::is_empty_v<T>)
                return nullptr;
            else
                return &r.get<T>(e);
        };
        if constexpr (std::is_default_constructible_v<T>)
            info->add = [](entt::registry& r, entt::entity e) { r.emplace<T>(e); };
        else
            info->addable = false;
        info->remove = [](entt::registry& r, entt::entity e) { r.remove<T>(e); };
        m_infos.push_back(std::move(info));
        return *m_infos.back();
    }

    const std::vector<std::unique_ptr<ComponentInfo>>& all() const { return m_infos; }
};
