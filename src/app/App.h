#pragma once

#include "app/CameraController.h"
#include "app/Overlay.h"
#include "render/Renderer.h"
#include "scene/Camera.h"
#include "scene/Scene.h"

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
    std::unique_ptr<Overlay> m_overlay;
    Scene m_scene;
    bool m_running = false;

    void handleEvent(const SDL_Event& event);
    void onResize(int width, int height);
    bool buildDemoScene();
    void syncRenderInstances();

public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Call before init
    void setOverlay(std::unique_ptr<Overlay> overlay) { m_overlay = std::move(overlay); }

    bool init(const char* title, int width, int height);
    void run();
};
