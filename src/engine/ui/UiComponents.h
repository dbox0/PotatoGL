#pragma once

#include "engine/assets/AssetTypes.h"

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

class Font;

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

enum class TextAlignH
{
    Left,
    Center,
    Right,
};

enum class TextAlignV
{
    Top,
    Middle,
    Bottom,
};

enum class TextOverflow
{
    Overflow,  // lines past the rect's bottom still draw
    Truncate,  // they don't, nor (without wrap) glyphs past its right edge
};

struct UIText
{
    std::string text;
    std::string font;  // empty = AssetCache::DefaultFont
    float fontSize = 32.0f;
    glm::vec4 color{1.0f};  // sRGB
    TextAlignH alignH = TextAlignH::Left;
    TextAlignV alignV = TextAlignV::Top;
    bool wrap = true;
    TextOverflow overflow = TextOverflow::Overflow;
    float lineSpacing = 1.0f;
};

// UIRenderer's laid-out glyphs for a UIText, rebuilt when anything they depend on changes.
// Runtime only, not registered.
struct UITextLayout
{
    struct Quad
    {
        glm::vec2 min, max;      // from the rect's min, in layout units
        glm::vec2 atlasMin, atlasMax;  // atlas pixels
    };

    std::string text;
    std::string font;
    const Font* fontPtr = nullptr;
    float fontSize = 0.0f;
    float lineSpacing = 0.0f;
    glm::vec2 size{-1.0f};
    TextAlignH alignH = TextAlignH::Left;
    TextAlignV alignV = TextAlignV::Top;
    bool wrap = false;
    TextOverflow overflow = TextOverflow::Overflow;
    std::vector<Quad> quads;
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

// INTERACTION (Play mode only)

// A click is a press and a release over the same button. Emits UIClicked and, if onClick is
// set, NamedEvent{onClick, sender = the button}.
struct UIButton
{
    bool interactable = true;
    entt::entity target = entt::null;  // the graphic it tints; null = itself
    glm::vec4 normal{1.0f};
    glm::vec4 hover{0.9f, 0.9f, 0.9f, 1.0f};
    glm::vec4 pressed{0.7f, 0.7f, 0.7f, 1.0f};
    glm::vec4 disabled{0.5f, 0.5f, 0.5f, 0.5f};
    float fadeDuration = 0.1f;
    std::string onClick;  // named event; empty = none
};

enum class SliderDirection
{
    LeftToRight,
    RightToLeft,
    BottomToTop,
    TopToBottom,
};

// The pointer's position along the direction sets the value; a drag keeps it until release.
// Each change emits UISliderChanged and NamedEvent{onChanged, value, max}. fill and handle
// follow the value in Edit mode too.
struct UISlider
{
    bool interactable = true;
    float min = 0.0f;
    float max = 1.0f;
    float value = 0.5f;
    bool wholeNumbers = false;
    SliderDirection direction = SliderDirection::LeftToRight;
    entt::entity fill = entt::null;    // a Filled UIImage, else a rect whose anchors follow the value
    entt::entity handle = entt::null;  // its anchors follow the value
    std::string onChanged;
};

struct UIClicked
{
    entt::entity button;
};

struct UISliderChanged
{
    entt::entity slider;
    float value;
};

// Runtime only, not registered
struct UIInteraction
{
    bool hovered = false;
    bool pressed = false;
    bool captured = false;
};

// Multiplies the colors of the graphics on its entity. Runtime only, written by buttons.
struct UITint
{
    glm::vec4 color{1.0f};
};

// A value bar: writes a Filled UIImage's fillAmount (and color, with gradient) and formats a
// label. listen picks up value and max from named events of that name.
struct UIBar
{
    entt::entity fill = entt::null;
    entt::entity label = entt::null;      // optional UIText
    std::string labelFormat = "{0}/{1}";  // {0} = value, {1} = max, {2} = percent
    float value = 100.0f;
    float max = 100.0f;
    std::string listen;
    float smoothing = 10.0f;  // 0 = jump to the value
    glm::vec4 lowColor{0.9f, 0.2f, 0.2f, 1.0f};
    glm::vec4 highColor{0.2f, 0.9f, 0.3f, 1.0f};
    bool gradient = false;  // tint the fill from low to high by the ratio

    float shown = 1.0f;              // runtime: the ratio drawn
    std::uint32_t listenHash = 0;    // runtime: of listen, 0 = not hashed yet
};
