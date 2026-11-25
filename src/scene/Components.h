#pragma once

#include "render/Renderer.h"

#include <string>

struct Name
{
    std::string value;
};

struct MeshRenderer
{
    ModelId model = InvalidModel;
};
