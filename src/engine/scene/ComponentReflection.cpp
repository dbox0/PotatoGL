#include "ComponentReflection.h"

#include "engine/reflect/ComponentRegistry.h"
#include "engine/scene/Components.h"
#include "engine/scene/Transform.h"

#include <glm/gtc/quaternion.hpp>

void registerSceneComponents(ComponentRegistry& components)
{
    components.add<Name>("Name").locked().field<&Name::value>("value", "Name");

    // The quaternion is what's saved; the inspector edits Euler angles, which round-trip
    // through it and can flip near +-90 deg pitch
    components.add<Transform>("Transform")
        .locked()
        .property(
            "position", "Position", FieldType::Vec3,
            [](const void* c) -> FieldValue { return static_cast<const Transform*>(c)->getPos(); },
            [](void* c, const FieldValue& v) { static_cast<Transform*>(c)->setPos(std::get<glm::vec3>(v)); })
        .property(
            "rotation", "Rotation", FieldType::Quat,
            [](const void* c) -> FieldValue { return static_cast<const Transform*>(c)->getRot(); },
            [](void* c, const FieldValue& v) { static_cast<Transform*>(c)->setRot(std::get<glm::quat>(v)); })
        .hidden()
        .property(
            "euler", "Rotation", FieldType::Vec3,
            [](const void* c) -> FieldValue {
                return glm::degrees(glm::eulerAngles(static_cast<const Transform*>(c)->getRot()));
            },
            [](void* c, const FieldValue& v) {
                static_cast<Transform*>(c)->setEulerAngles(glm::radians(std::get<glm::vec3>(v)));
            },
            0.5f)
        .notSaved()
        .property(
            "scale", "Scale", FieldType::Vec3,
            [](const void* c) -> FieldValue { return static_cast<const Transform*>(c)->getScale(); },
            [](void* c, const FieldValue& v) { static_cast<Transform*>(c)->setScale(std::get<glm::vec3>(v)); },
            0.01f);

    components.add<MeshRenderer>("MeshRenderer")
        .property(
            "mesh", "Mesh", FieldType::String,
            [](const void* c) -> FieldValue { return static_cast<const MeshRenderer*>(c)->mesh; },
            [](void* c, const FieldValue& v) {
                auto* renderer = static_cast<MeshRenderer*>(c);
                renderer->mesh = std::get<std::string>(v);
                renderer->model = InvalidModel;
            })
        .property(
            "model", "Model", FieldType::Int,
            [](const void* c) -> FieldValue { return static_cast<int>(static_cast<const MeshRenderer*>(c)->model); },
            nullptr);

    components.add<CameraComponent>("Camera")
        .field<&CameraComponent::fovDeg>("fovDeg", "FOV", 0.5f)
        .field<&CameraComponent::nearPlane>("nearPlane", "Near", 0.01f)
        .field<&CameraComponent::farPlane>("farPlane", "Far", 1.0f)
        .field<&CameraComponent::orthographic>("orthographic")
        .field<&CameraComponent::orthoHeight>("orthoHeight", 0.1f);

    components.add<Disabled>("Disabled");
}
