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
    entt::entity prevSibling = entt::null;
    entt::entity nextSibling = entt::null;
    std::size_t childCount = 0;
};

struct WorldTransform
{
    glm::mat4 matrix = glm::mat4(1.0f);
};
