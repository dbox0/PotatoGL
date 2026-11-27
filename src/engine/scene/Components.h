#pragma once

#include "engine/render/Renderer.h"

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <cstddef>
#include <string>

struct Name
{
    std::string value;
};

struct MeshRenderer
{
    ModelId model = InvalidModel;
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

// Tags: empty types, EnTT stores no data for them
struct Disabled {};  // skipped by gameplay systems and scripts
struct Dead {};      // destroyed at the end of the gameplay step
struct Selected {};  // editor selection; it and its descendants get an outline. Never saved.
