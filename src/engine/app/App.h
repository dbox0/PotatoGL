#pragma once

#include "engine/app/CameraController.h"
#include "engine/assets/AssetCache.h"
#include "engine/app/Game.h"
#include "engine/app/Overlay.h"
#include "engine/app/PlayMode.h"
#include "engine/reflect/ComponentRegistry.h"
#include "engine/render/Renderer.h"
#include "engine/render/UIRenderer.h"
#include "engine/scene/Camera.h"
#include "engine/scene/Scene.h"
#include "engine/ui/UiSystems.h"

#include <SDL3/SDL.h>

#include <memory>

class App
{
private:
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<UIRenderer> m_ui;
    AssetCache m_assets;
    Camera m_camera;
    CameraController m_controller;
    ComponentRegistry m_components;
    std::unique_ptr<Overlay> m_overlay;
    std::unique_ptr<Game> m_game;
    Scene m_scene;
    UiBarSystem m_uiBars;  // after m_scene: disconnects from its bus first
    PlayMode m_playMode = PlayMode::Play;
    PlayMode m_lastPlayMode = PlayMode::Play;
    bool m_animationPreview = false;
    int m_fbWidth = 1;
    int m_fbHeight = 1;
    bool m_running = false;

    void handleEvent(const SDL_Event& event);
    void onResize(int width, int height);
    GameContext gameContext() { return {*m_renderer, m_scene, m_camera, m_components}; }
    OverlayContext overlayContext()
    {
        return {*m_renderer, *m_ui, m_assets, m_scene, m_camera, m_controller, m_components, m_playMode, m_animationPreview};
    }
    Scene& activeScene();
    void updateAnimators(Scene& scene, float dt);
    void syncRenderInstances(Scene& scene);
    void syncRenderLights(const Scene& scene);
    void resolveUiImages(Scene& scene);
    void renderToWindow();
    void updateInput();
    bool gameViewSize(glm::vec2& size) const;

public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Call before init
    void setOverlay(std::unique_ptr<Overlay> overlay) { m_overlay = std::move(overlay); }
    void setGame(std::unique_ptr<Game> game)          { m_game = std::move(game); }

    bool init(const char* title, int width, int height);
    void run();
};
