#pragma once

#include "engine/app/PlayMode.h"

#include <SDL3/SDL.h>

class AssetCache;
class Camera;
class CameraController;
class ComponentRegistry;
class Renderer;
class Scene;
class UIRenderer;

struct OverlayContext
{
    Renderer& renderer;
    UIRenderer& ui;
    AssetCache& assets;
    Scene& scene;
    Camera& camera;  // editor camera
    CameraController& controller;
    ComponentRegistry& components;
    PlayMode& playMode;
    bool& animationPreview;  // Animators play in Edit mode; nothing is saved or recorded
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
    // Where the game is shown, in window coordinates. false: it isn't shown.
    virtual bool gameViewRect(float& x, float& y, float& w, float& h) const = 0;
    virtual bool gameViewHovered() const = 0;    // gameplay Input sees mouse buttons over it
    // A scene of the overlay's own to animate, sync and render instead of the game's (a viewer);
    // nullptr for the game's. It's only evaluated: the game doesn't update it, time doesn't advance it.
    virtual Scene* activeScene() { return nullptr; }

    // The window was asked to close. false: not yet (e.g. unsaved changes); App then quits
    // once quitRequested() turns true.
    virtual bool confirmQuit() { return true; }
    virtual bool quitRequested() const { return false; }

    virtual void update(const OverlayContext& context) = 0;       // builds the UI, before the game updates
    virtual void renderViews(const OverlayContext& context) = 0;  // draws the scene into the UI's views
    virtual void render() = 0;
};
