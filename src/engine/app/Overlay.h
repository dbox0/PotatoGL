#pragma once

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
    Camera& camera;
    CameraController& controller;
    ComponentRegistry& components;
};

// Optional UI drawn on top of the scene (the editor). App works without one.
class Overlay
{
public:
    virtual ~Overlay() = default;

    virtual bool init(SDL_Window* window, SDL_GLContext context) = 0;
    virtual void shutdown() = 0;  // must run before the GL context is destroyed

    virtual void processEvent(const SDL_Event& event) = 0;
    virtual bool wantsMouse() const = 0;
    virtual bool wantsKeyboard() const = 0;

    virtual void update(const OverlayContext& context) = 0;
    virtual void render() = 0;
};
