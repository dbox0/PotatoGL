#pragma once

#include <glm/glm.hpp>

struct ScriptContext;

// Example script: spins its object around an axis in parent space
struct Rotator
{
    glm::vec3 axis = glm::vec3(0.0f, 1.0f, 0.0f);
    float degreesPerSec = 90.0f;

    void onUpdate(ScriptContext& ctx, float dt);
};
