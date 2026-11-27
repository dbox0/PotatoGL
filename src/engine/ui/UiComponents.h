#pragma once

#include "engine/assets/AssetTypes.h"

#include <glm/glm.hpp>

#include <string>

// UI lives under a Canvas, laid out in canvas pixels: screen pixels / canvas scale,
// origin at the top-left, y down. Anchors and pivots are 0..1 of the parent rect, so
// (0, 0) is its top-left corner.

enum class CanvasScaleMode
{
    ConstantPixelSize,
    ScaleWithScreenSize,
};

// Screen-space overlay root; its Transform is ignored
struct Canvas
{
    glm::vec2 referenceResolution{1920.0f, 1080.0f};
    CanvasScaleMode scaleMode = CanvasScaleMode::ScaleWithScreenSize;
    float matchWidthOrHeight = 0.5f;  // 0 = width, 1 = height
    int sortOrder = 0;                // higher draws on top
};

struct RectTransform
{
    glm::vec2 anchorMin{0.5f};
    glm::vec2 anchorMax{0.5f};
    glm::vec2 pivot{0.5f};
    glm::vec2 anchoredPosition{0.0f};  // pivot offset from its point between the anchors
    glm::vec2 sizeDelta{100.0f};       // size minus the anchors' span
    float rotationDeg = 0.0f;          // counter-clockwise, around the pivot
    glm::vec2 scale{1.0f};
};

// Written by uiLayoutSystem; runtime only, not registered
struct UIRect
{
    glm::vec2 min{0.0f};          // in the parent's layout space, before this element's rotation and scale
    glm::vec2 max{0.0f};
    glm::mat3 toScreen{1.0f};     // min/max space to target pixels, including every rotation, scale and the canvas scale
    int drawOrder = 0;
};

enum class UIShapeType
{
    Rect,
    RoundedRect,
    Circle,  // the largest circle centred in the rect
    Line,    // from the rect's left-centre to its right-centre
};

enum class UIImageType
{
    Simple,
    Sliced,  // 9 quads: the sprite's .meta border stays unscaled
    Filled,
};

enum class UIFillMethod
{
    Horizontal,
    Vertical,
    Radial360,  // clockwise
};

enum class UIFillOrigin
{
    LeftOrBottom,  // radial: starts at the bottom
    RightOrTop,
};

// Without a sprite it's a plain rect of color
struct UIImage
{
    std::string sprite;
    glm::vec4 color{1.0f};  // sRGB
    UIImageType type = UIImageType::Simple;
    float fillAmount = 1.0f;
    UIFillMethod fillMethod = UIFillMethod::Horizontal;
    UIFillOrigin fillOrigin = UIFillOrigin::LeftOrBottom;
    bool preserveAspect = false;  // Simple and Filled

    // Runtime, resolved by App each frame; texture is reset when sprite changes
    TextureId texture = InvalidTexture;
    glm::vec2 textureSize{0.0f};
    glm::vec4 border{0.0f};
};

struct UIShape
{
    UIShapeType shape = UIShapeType::Rect;
    glm::vec4 color{1.0f};  // sRGB
    float cornerRadius = 8.0f;
    float borderWidth = 0.0f;
    glm::vec4 borderColor{0.0f, 0.0f, 0.0f, 1.0f};
    float lineWidth = 2.0f;
};
