#include "Editor.h"

#include "engine/render/Renderer.h"
#include "engine/scene/Camera.h"
#include "engine/scene/Scene.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

Editor::~Editor()
{
    shutdown();
}

bool Editor::init(SDL_Window* window, SDL_GLContext context)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;  // dragging in a viewport is for gizmos, not the window
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
    m_sceneView.release();
    m_gameView.release();
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

bool Editor::editorCameraInput() const
{
    return m_visible && m_sceneView.hovered() && !ImGui::GetIO().WantTextInput;
}

bool Editor::gameInput() const
{
    return !m_visible || (m_gameView.focused() && !ImGui::GetIO().WantTextInput);
}

void Editor::update(const OverlayContext& context)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    if (!m_visible)
        return;

    drawToolbar(context.playMode);
    drawDockSpace();
    m_debugPanel.draw(context);
    m_hierarchyPanel.draw(context.scene, m_selected);
    m_inspectorPanel.draw(context.scene, context.components, m_selected);
    // The tab bar only exists after the first frame, and it selects its last tab
    if (m_selectSceneTab && ImGui::GetFrameCount() > 1) {
        m_sceneView.focus();
        m_selectSceneTab = false;
    }
    m_sceneView.draw(context, m_selected);
    m_gameView.draw(context.scene.gameCamera() == entt::null ? "No camera in the scene" : nullptr);
    syncSelectedTag(context.scene.registry());
}

void Editor::syncSelectedTag(entt::registry& registry)
{
    if (m_selected != entt::null && !registry.valid(m_selected))
        m_selected = entt::null;
    if (m_selected == m_tagged)
        return;
    if (m_tagged != entt::null && registry.valid(m_tagged))
        registry.remove<Selected>(m_tagged);
    if (m_selected != entt::null)
        registry.emplace_or_replace<Selected>(m_selected);
    m_tagged = m_selected;
}

void Editor::renderViews(const OverlayContext& context)
{
    m_sceneView.render(context, m_selected);
    syncSelectedTag(context.scene.registry());
    if (m_gameView.visible()) {
        if (const auto viewProj = context.scene.gameCameraViewProj(m_gameView.aspect()))
            context.renderer.render(*viewProj, m_gameView.target());
    }
}

void Editor::render()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// Builds the default layout the first time (no imgui.ini entry yet)
void Editor::drawDockSpace()
{
    const ImGuiID dockId = ImGui::GetID("EditorDockSpace");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!ImGui::DockBuilderGetNode(dockId)) {
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, viewport->WorkSize);
        ImGuiID center = dockId;
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
        ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.6f, nullptr, &right);
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
        ImGui::DockBuilderDockWindow(m_sceneView.name(), center);
        ImGui::DockBuilderDockWindow(m_gameView.name(), center);
        ImGui::DockBuilderDockWindow("Scene Hierarchy", right);
        ImGui::DockBuilderDockWindow("Inspector", rightBottom);
        ImGui::DockBuilderDockWindow("Debug", left);
        ImGui::DockBuilderFinish(dockId);
        m_selectSceneTab = true;
    }
    ImGui::DockSpaceOverViewport(dockId, viewport);
}

// Stop keeps the scene as it is; restoring the pre-Play state needs serialization
void Editor::drawToolbar(PlayMode& mode)
{
    if (!ImGui::BeginMainMenuBar())
        return;

    const bool playing = mode != PlayMode::Edit;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = ImGui::CalcTextSize("Stop").x + ImGui::CalcTextSize("Resume").x +
                        style.FramePadding.x * 4.0f + style.ItemSpacing.x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);

    if (playing)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.8f, 1.0f));
    if (ImGui::Button(playing ? "Stop" : "Play")) {
        mode = playing ? PlayMode::Edit : PlayMode::Play;
        if (playing)
            m_sceneView.focus();
        else
            m_gameView.focus();
    }
    if (playing)
        ImGui::PopStyleColor();

    ImGui::BeginDisabled(!playing);
    if (ImGui::Button(mode == PlayMode::Paused ? "Resume" : "Pause"))
        mode = mode == PlayMode::Paused ? PlayMode::Play : PlayMode::Paused;
    ImGui::EndDisabled();

    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::CalcTextSize("Playing").x - style.ItemSpacing.x * 2.0f);
    ImGui::TextDisabled("%s", mode == PlayMode::Edit ? "Edit" : mode == PlayMode::Play ? "Playing" : "Paused");
    ImGui::EndMainMenuBar();
}
