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
        .asset(AssetKind::Model)
        .property(
            "material", "Material", FieldType::String,
            [](const void* c) -> FieldValue { return static_cast<const MeshRenderer*>(c)->material; },
            [](void* c, const FieldValue& v) {
                auto* renderer = static_cast<MeshRenderer*>(c);
                renderer->material = std::get<std::string>(v);
                renderer->materialId = InvalidMaterial;
            })
        .asset(AssetKind::Material)
        .property(
            "model", "Model", FieldType::Int,
            [](const void* c) -> FieldValue { return static_cast<int>(static_cast<const MeshRenderer*>(c)->model); },
            nullptr)
        .hidden();

    components.add<SkinnedMeshRenderer>("SkinnedMeshRenderer")
        .property(
            "mesh", "Mesh", FieldType::String,
            [](const void* c) -> FieldValue { return static_cast<const SkinnedMeshRenderer*>(c)->mesh; },
            [](void* c, const FieldValue& v) {
                auto* renderer = static_cast<SkinnedMeshRenderer*>(c);
                renderer->mesh = std::get<std::string>(v);
                renderer->model = InvalidModel;
            })
        .asset(AssetKind::Model)
        .property(
            "material", "Material", FieldType::String,
            [](const void* c) -> FieldValue { return static_cast<const SkinnedMeshRenderer*>(c)->material; },
            [](void* c, const FieldValue& v) {
                auto* renderer = static_cast<SkinnedMeshRenderer*>(c);
                renderer->material = std::get<std::string>(v);
                renderer->materialId = InvalidMaterial;
            })
        .asset(AssetKind::Material);

    components.add<Animator>("Animator")
        .property(
            "clip", "Clip", FieldType::String,
            [](const void* c) -> FieldValue { return static_cast<const Animator*>(c)->clip; },
            [](void* c, const FieldValue& v) {
                auto* animator = static_cast<Animator*>(c);
                animator->clip = std::get<std::string>(v);
                animator->resolved = false;
            })
        .asset(AssetKind::Animation)
        .field<&Animator::speed>("speed", 0.01f)
        .field<&Animator::loop>("loop")
        .field<&Animator::playOnStart>("playOnStart");

    components.add<BoneSocket>("BoneSocket")
        .property(
            "joint", "Joint", FieldType::String,
            [](const void* c) -> FieldValue { return static_cast<const BoneSocket*>(c)->joint; },
            [](void* c, const FieldValue& v) {
                auto* socket = static_cast<BoneSocket*>(c);
                socket->joint = std::get<std::string>(v);
                socket->resolvedFor = nullptr;
            })
        .joint();

    components.add<CameraComponent>("Camera")
        .field<&CameraComponent::fovDeg>("fovDeg", "FOV", 0.5f)
        .field<&CameraComponent::nearPlane>("nearPlane", "Near", 0.01f)
        .field<&CameraComponent::farPlane>("farPlane", "Far", 1.0f)
        .field<&CameraComponent::orthographic>("orthographic")
        .field<&CameraComponent::orthoHeight>("orthoHeight", 0.1f);

    components.add<DirectionalLight>("DirectionalLight")
        .field<&DirectionalLight::color>("color", 0.01f).color()
        .field<&DirectionalLight::intensity>("intensity", 0.01f);

    components.add<PointLight>("PointLight")
        .field<&PointLight::color>("color", 0.01f).color()
        .field<&PointLight::intensity>("intensity", 0.01f)
        .field<&PointLight::range>("range", 0.1f);

    components.add<SpotLight>("SpotLight")
        .field<&SpotLight::color>("color", 0.01f).color()
        .field<&SpotLight::intensity>("intensity", 0.01f)
        .field<&SpotLight::range>("range", 0.1f)
        .field<&SpotLight::innerDeg>("innerDeg", "Inner Angle", 0.5f)
        .field<&SpotLight::outerDeg>("outerDeg", "Outer Angle", 0.5f);

    components.add<Disabled>("Disabled");

    // Removing it unpacks the instance: a plain copy from then on
    components.add<PrefabInstance>("PrefabInstance")
        .label("Prefab Instance")
        .field<&PrefabInstance::prefab>("prefab", "Prefab")
        .asset(AssetKind::Prefab)
        .addable = false;
}
