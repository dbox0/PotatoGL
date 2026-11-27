#pragma once

#include "engine/app/PlayMode.h"

#include <SDL3/SDL.h>

class Camera;
class CameraController;
class ComponentRegistry;
class Renderer;
class Scene;

struct OverlayContext
{
    Renderer& renderer;
    Scene& scene;
    Camera& camera;  // editor camera
    CameraController& controller;
    ComponentRegistry& components;
    PlayMode& playMode;
};

// Optional UI that owns the window (the editor). App works without one.
class Overlay
{
public:
    virtual ~Overlay() = default;

    virtual bool init(SDL_Window* window, SDL_GLContext context) = 0;
    virtual void shutdown() = 0;  // must run before the GL context is destroyed

    virtual void processEvent(const SDL_Event& event) = 0;
    virtual bool editorCameraInput() const = 0;  // the editor camera may take clicks and keys
    virtual bool gameInput() const = 0;          // gameplay Input sees the keyboard
    virtual bool visible() const = 0;            // false: App draws the game camera to the window

    virtual void update(const OverlayContext& context) = 0;       // builds the UI, before the game updates
    virtual void renderViews(const OverlayContext& context) = 0;  // draws the scene into the UI's views
    virtual void render() = 0;
};
