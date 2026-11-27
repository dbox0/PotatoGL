#include "ComponentReflection.h"

#include "engine/reflect/ComponentRegistry.h"
#include "engine/scene/Components.h"
#include "engine/scene/Transform.h"

#include <glm/gtc/quaternion.hpp>

void registerSceneComponents(ComponentRegistry& components)
{
    components.add<Name>("Name").locked().field<&Name::value>("Name");

    components.add<Transform>("Transform")
        .locked()
        .property(
            "Position", FieldType::Vec3,
            [](const void* c) -> FieldValue { return static_cast<const Transform*>(c)->getPos(); },
            [](void* c, const FieldValue& v) { static_cast<Transform*>(c)->setPos(std::get<glm::vec3>(v)); })
        // Euler round-trips through the quaternion, so values can flip near +-90 deg pitch
        .property(
            "Rotation", FieldType::Vec3,
            [](const void* c) -> FieldValue {
                return glm::degrees(glm::eulerAngles(static_cast<const Transform*>(c)->getRot()));
            },
            [](void* c, const FieldValue& v) {
                static_cast<Transform*>(c)->setEulerAngles(glm::radians(std::get<glm::vec3>(v)));
            },
            0.5f)
        .property(
            "Scale", FieldType::Vec3,
            [](const void* c) -> FieldValue { return static_cast<const Transform*>(c)->getScale(); },
            [](void* c, const FieldValue& v) { static_cast<Transform*>(c)->setScale(std::get<glm::vec3>(v)); },
            0.01f);

    components.add<MeshRenderer>("Mesh Renderer")
        .property(
            "Model", FieldType::Int,
            [](const void* c) -> FieldValue { return static_cast<int>(static_cast<const MeshRenderer*>(c)->model); },
            nullptr)
        .addable = false;  // needs a ModelId, so only code can add one

    components.add<CameraComponent>("Camera")
        .field<&CameraComponent::fovDeg>("FOV", 0.5f)
        .field<&CameraComponent::nearPlane>("Near", 0.01f)
        .field<&CameraComponent::farPlane>("Far", 1.0f)
        .field<&CameraComponent::orthographic>("Orthographic")
        .field<&CameraComponent::orthoHeight>("Ortho height", 0.1f);

    components.add<Disabled>("Disabled");
}
