#include "Editor.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

Editor::~Editor()
{
    shutdown();
}

bool Editor::init(SDL_Window* window, SDL_GLContext context)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForOpenGL(window, context) || !ImGui_ImplOpenGL3_Init("#version 430 core")) {
        ImGui::DestroyContext();
        return false;
    }
    m_initialized = true;
    m_debugPanel.init();
    return true;
}

void Editor::shutdown()
{
    if (!m_initialized)
        return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void Editor::processEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1 && !event.key.repeat)
        m_visible = !m_visible;
    ImGui_ImplSDL3_ProcessEvent(&event);
}

bool Editor::wantsMouse() const
{
    return m_visible && ImGui::GetIO().WantCaptureMouse;
}

bool Editor::wantsKeyboard() const
{
    return m_visible && ImGui::GetIO().WantCaptureKeyboard;
}

void Editor::update(const OverlayContext& context)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (!m_visible)
        return;
    m_debugPanel.draw(context);
    m_hierarchyPanel.draw(context.scene);
}

void Editor::render()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
