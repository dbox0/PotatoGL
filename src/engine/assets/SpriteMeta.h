#pragma once

#include <glm/glm.hpp>

#include <string>

enum class SpriteFilter
{
    Linear,
    Point,
};

// Stored next to the image as "<image>.meta" (JSON); a missing file means the defaults
struct SpriteMeta
{
    glm::vec4 border{0.0f};  // left, top, right, bottom, in texture pixels: Sliced images keep them unscaled
    float pixelsPerUnit = 100.0f;
    SpriteFilter filter = SpriteFilter::Linear;
};

inline std::string spriteMetaPath(const std::string& imagePath) { return imagePath + ".meta"; }

// file: a filesystem path to the image itself, not to its .meta
SpriteMeta loadSpriteMeta(const std::string& file);
bool saveSpriteMeta(const std::string& file, const SpriteMeta& meta);
