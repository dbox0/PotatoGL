#pragma once

#include "engine/assets/Model.h"

// Also loads .obj; output is converted to right-handed, Y up, meters
std::optional<ModelData> loadFbx(const std::string& path);
