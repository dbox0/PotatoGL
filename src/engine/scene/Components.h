#pragma once

#include "engine/anim/Clip.h"
#include "engine/anim/Pose.h"
#include "engine/anim/Skeleton.h"
#include "engine/assets/AssetTypes.h"
#include "engine/render/Renderer.h"

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct Name
{
    std::string value;
};

// mesh and material are what gets saved; App resolves them through the AssetCache
struct MeshRenderer
{
    std::string mesh;              // model path relative to the resource dir, e.g. "assets/Duck.glb"
    ModelId model = InvalidModel;  // runtime handle, reset whenever mesh changes
    std::string material;          // .mat path; empty = the model's own materials
    MaterialId materialId = InvalidMaterial;  // runtime handle, reset whenever material changes
};

// A textured quad in the local XY plane, centred on the object, facing +Z. Never casts shadows.
// Unlit (the default): color * texture goes to the HDR target as-is, so exposure and the
// tonemap curve apply to it like to everything else.
struct SpriteRenderer
{
    std::string sprite;
    glm::vec4 color{1.0f};       // sRGB
    glm::vec2 size{0.0f};        // world units; 0 = the texture size / pixelsPerUnit
    bool flipX = false;
    bool flipY = false;
    int sortOrder = 0;           // lower draws first; then back to front
    bool lit = false;            // diffuse lighting from the sun, lights and ambient
    bool receiveShadows = true;  // only when lit
    TextureId texture = InvalidTexture;  // runtime, reset whenever sprite changes
    glm::vec2 drawnSize{0.0f};           // runtime: the size last submitted, for the editor
};

// On the root of a prefab instance: the .prefab it's rebuilt from when that changes
struct PrefabInstance
{
    std::string prefab;
};

// Stable identity: saved with the scene and kept across save/load, Play/Stop and undo,
// unlike entt::entity handles. 0 means none (and stands for the root in references).
struct Uuid
{
    std::uint64_t value = 0;
};

// Intrusive doubly linked child list; children are kept in insertion order
struct Relationship
{
    entt::entity parent = entt::null;
    entt::entity firstChild = entt::null;
    entt::entity lastChild = entt::null;
    entt::entity prevSibling = entt::null;
    entt::entity nextSibling = entt::null;
    std::size_t childCount = 0;
};

// The game view renders through the first enabled one; the editor has its own camera
struct CameraComponent
{
    float fovDeg = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    bool orthographic = false;
    float orthoHeight = 10.0f;

    glm::mat4 projection(float aspect) const
    {
        return makeProjection(orthographic, glm::radians(fovDeg), orthoHeight, aspect, nearPlane, farPlane);
    }
};

struct WorldTransform
{
    glm::mat4 matrix = glm::mat4(1.0f);
};


// LIGHTS
struct DirectionalLight{
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
};

struct PointLight{
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
};

struct SpotLight{
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float innerDeg = 20.0f;
    float outerDeg = 30.0f;
};

// ANIMATION
// Saved fields first; the rest is runtime state, rebuilt by the AnimationSystem after load,
// copy or undo like MeshRenderer::model

// Without an Animator it shows the bind pose
struct SkinnedMeshRenderer
{
    std::string mesh;
    std::string material;
    ModelId model = InvalidModel;
    MaterialId materialId = InvalidMaterial;
    SkinId skin = InvalidSkin;
    std::shared_ptr<const SkeletonData> skeleton;
    std::vector<Affine> jointModel;  // current pose, model space; sockets and gizmos read it
    bool posed = false;              // jointModel isn't the bind pose
};

// Plays a clip on the SkinnedMeshRenderer of its own entity
struct Animator
{
    std::string clip;  // "assets/Fox.glb#Run"
    float speed = 1.0f;
    bool loop = true;
    bool playOnStart = true;

    bool playing = false;  // gameplay may start and stop it
    float time = 0.0f;
    bool started = false;  // playOnStart was applied
    bool resolved = false; // clip looked up since it last changed
    bool evaluated = false;
    std::uint64_t clipVersion = 0;  // AssetCache::clipVersion() when the clip was resolved
    std::shared_ptr<const ClipData> clipData;
    // Local transforms put over the sampled pose (the Animation Viewer's Poser); never saved
    std::vector<std::pair<int, JointTrs>> overrides;
    const SkeletonData* boundTo = nullptr;
    std::vector<std::int16_t> binding;
    Pose pose;
};

// On a child of a skinned object: follows one of its joints. Its Transform is driven, not edited.
struct BoneSocket
{
    std::string joint;
    int jointIndex = -1;
    const SkeletonData* resolvedFor = nullptr;
};


struct ScriptComponent
{
    std::string script;
};

struct Collider
{

};

struct Rigidbody{};
struct CharacterController{};
struct AnimatorStateMachine{};

struct AudioListener{};
struct AudioSource{};

struct Tag{};
struct Layer{};
// Tags: empty types, EnTT stores no data for them
struct Disabled {};  // skipped by gameplay systems and scripts
struct Dead {};      // destroyed at the end of the gameplay step
struct Selected {};  // editor selection; it and its descendants get an outline. Never saved.
