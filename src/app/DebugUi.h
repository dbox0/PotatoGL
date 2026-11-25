#pragma once

#include <SDL3/SDL.h>

#include <string>

class Camera;
class CameraController;
class Renderer;

// Dear ImGui overlay; F1 toggles it
class DebugUi
{
private:
    bool m_initialized = false;
    bool m_visible = true;
    bool m_showDemo = false;
    bool m_autoReload = true;
    bool m_vsync = true;
    std::string m_glVersion;
    std::string m_glRenderer;

public:
    DebugUi() = default;
    ~DebugUi();

    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    bool init(SDL_Window* window, SDL_GLContext context);
    void shutdown();  // must run before the GL context is destroyed

    void processEvent(const SDL_Event& event);
    bool wantsMouse() const;
    bool wantsKeyboard() const;
    bool autoReloadShaders() const { return m_autoReload; }

    void beginFrame();
    void draw(Renderer& renderer, Camera& camera, CameraController& controller);
    void render();
};
