#pragma once

#include "engine/core/EventBus.h"

#include <glm/glm.hpp>

class Input;
class Scene;
struct NamedEvent;

// Play mode only, after uiLayoutSystem at the game view's size, before the game update so
// gameplay sees Input::pointerOverUi for this frame's clicks. Buttons tint and click, sliders
// drag; events go out through the scene's EventBus.
void uiInputSystem(Scene& scene, Input& input, float dt);

// Every frame, Edit mode too: slider fills and handles follow their values
void uiSliderVisualSystem(Scene& scene);

// Bars pick up named events through the bus. Its address must not change once attached.
class UiBarSystem
{
private:
    Scene* m_scene = nullptr;
    Connection m_connection;

    void onNamed(const NamedEvent& event);

public:
    void attach(Scene& scene, EventBus& bus);
    // playing: smooth towards the value; else jump, so Edit mode previews it
    void update(Scene& scene, float dt, bool playing);
};
