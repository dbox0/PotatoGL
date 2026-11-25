#pragma once

#include "engine/app/CameraController.h"
#include "engine/app/Game.h"
#include "engine/app/Overlay.h"
#include "engine/reflect/ComponentRegistry.h"
#include "engine/render/Renderer.h"
#include "engine/scene/Camera.h"
#include "engine/scene/Scene.h"

#include <SDL3/SDL.h>

#include <memory>

class App
{
private:
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    std::unique_ptr<Renderer> m_renderer;
    Camera m_camera;
    CameraController m_controller;
    ComponentRegistry m_components;
    std::unique_ptr<Overlay> m_overlay;
    std::unique_ptr<Game> m_game;
    Scene m_scene;
    bool m_running = false;

    void handleEvent(const SDL_Event& event);
    void onResize(int width, int height);
    GameContext gameContext() { return {*m_renderer, m_scene, m_camera, m_components}; }
    void syncRenderInstances();

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
