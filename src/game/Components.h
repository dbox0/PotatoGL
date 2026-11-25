#pragma once

#include <glm/glm.hpp>

// Here: Gameplay data only;
// logic lives in Systems.cpp


struct Spin
{
    float radiansPerSec = 0.0f;
};

struct Velocity
{
    glm::vec3 value = glm::vec3(0.0f);
};

struct Lifetime
{
    float remaining = 0.0f;
};

// Tags: empty types, EnTT stores no data for them
struct Disabled {};
struct Dead {};
