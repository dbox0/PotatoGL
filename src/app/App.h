#pragma once

#include "app/CameraController.h"
#include "app/DebugUi.h"
#include "render/Renderer.h"
#include "scene/Camera.h"

#include <SDL3/SDL.h>
#include <entt/entt.hpp>

#include <memory>
#include <string>

class App
{
private:
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    std::unique_ptr<Renderer> m_renderer;
    Camera m_camera;
    CameraController m_controller;
    DebugUi m_debugUi;
    entt::registry m_registry;
    bool m_running = false;

    void handleEvent(const SDL_Event& event);
    void onResize(int width, int height);
    bool buildDemoScene();
    entt::entity createGameObject(std::string name, ModelId model, const glm::vec3& pos);
    void syncRenderInstances();

public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init(const char* title, int width, int height);
    void run();
};
