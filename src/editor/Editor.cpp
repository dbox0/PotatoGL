#include "Editor.h"

#include "engine/core/Paths.h"
#include "engine/reflect/ComponentRegistry.h"
#include "engine/render/Renderer.h"
#include "engine/scene/Camera.h"
#include "engine/scene/Scene.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/scene/Transform.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <cstdio>
#include <filesystem>

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
    m_window = window;
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

    applyDialogResult(context);
    updateTitle(context.scene.path());
    drawMenuBar(context);
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

static constexpr SDL_DialogFileFilter SceneFilter{"Scene", "scene"};

void Editor::drawMenuBar(const OverlayContext& context)
{
    const bool editing = context.playMode == PlayMode::Edit;
    if (editing) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal))
            newScene(context);
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal))
            showOpenDialog();
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
            saveScene(context, context.scene.path());
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
            showSaveDialog();
    }

    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File")) {
        ImGui::BeginDisabled(!editing);
        if (ImGui::MenuItem("New Scene", "Ctrl+N"))
            newScene(context);
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
            showOpenDialog();
        if (ImGui::MenuItem("Save", "Ctrl+S"))
            saveScene(context, context.scene.path());
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
            showSaveDialog();
        ImGui::EndDisabled();
        if (!editing)
            ImGui::TextDisabled("Stop playing to save or load");
        ImGui::EndMenu();
    }

    drawPlayButtons(context);

    const char* modeText = context.playMode == PlayMode::Edit   ? "Edit"
                           : context.playMode == PlayMode::Play ? "Playing"
                                                                : "Paused";
    const std::string& path = context.scene.path();
    const std::string sceneName = path.empty() ? "Untitled" : std::filesystem::path(path).filename().string();
    std::string text = sceneName + "  |  " + modeText;
    if (!m_status.empty() && ImGui::GetTime() - m_statusTime < 5.0)
        text = m_status + "  |  " + text;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::CalcTextSize(text.c_str()).x -
                         ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::EndMainMenuBar();
}

void Editor::drawPlayButtons(const OverlayContext& context)
{
    PlayMode& mode = context.playMode;
    const bool playing = mode != PlayMode::Edit;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = ImGui::CalcTextSize("Stop").x + ImGui::CalcTextSize("Resume").x +
                        style.FramePadding.x * 4.0f + style.ItemSpacing.x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);

    if (playing)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.8f, 1.0f));
    const bool toggled = ImGui::Button(playing ? "Stop" : "Play");
    if (playing)
        ImGui::PopStyleColor();
    if (toggled)
        setPlaying(context, !playing);

    ImGui::BeginDisabled(!playing);
    if (ImGui::Button(mode == PlayMode::Paused ? "Resume" : "Pause"))
        mode = mode == PlayMode::Paused ? PlayMode::Play : PlayMode::Paused;
    ImGui::EndDisabled();
}

// Play snapshots the scene; Stop restores it, so nothing done while playing sticks.
// The selection survives by its position in the file order.
void Editor::setPlaying(const OverlayContext& context, bool playing)
{
    if (playing) {
        std::vector<entt::entity> order;
        m_playSnapshot = ::saveScene(context.scene, context.components, &order);
        m_playSelection = SIZE_MAX;
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == m_selected)
                m_playSelection = i;
        }
        context.playMode = PlayMode::Play;
        m_gameView.focus();
        return;
    }

    context.playMode = PlayMode::Edit;
    std::vector<entt::entity> order;
    std::string error;
    if (loadScene(context.scene, context.components, m_playSnapshot, &error, &order))
        m_selected = m_playSelection < order.size() ? order[m_playSelection] : entt::null;
    else
        setStatus("Restore failed: " + error);
    m_playSnapshot.clear();
    m_sceneView.focus();
}

void Editor::newScene(const OverlayContext& context)
{
    Scene& scene = context.scene;
    scene.clear();
    scene.setPath("");
    m_selected = entt::null;

    const entt::entity camera = scene.createGameObject("Main Camera");
    Transform& transform = scene.registry().get<Transform>(camera);
    transform.setPos({0.0f, 2.0f, 10.0f});
    transform.lookAt({0.0f, 0.0f, 0.0f});
    scene.registry().emplace<CameraComponent>(camera);
}

void Editor::openScene(const OverlayContext& context, const std::string& path)
{
    std::string error;
    if (loadSceneFile(context.scene, context.components, path, &error)) {
        m_selected = entt::null;
        setStatus("Opened " + std::filesystem::path(path).filename().string());
    } else {
        setStatus("Open failed: " + error);
    }
}

void Editor::saveScene(const OverlayContext& context, const std::string& path)
{
    if (path.empty()) {
        showSaveDialog();
        return;
    }
    std::string error;
    if (saveSceneFile(context.scene, context.components, path, &error))
        setStatus("Saved " + std::filesystem::path(path).filename().string());
    else
        setStatus("Save failed: " + error);
}

void Editor::onDialog(void* userdata, const char* const* files, bool save)
{
    if (!files) {
        std::fprintf(stderr, "File dialog failed: %s\n", SDL_GetError());
        return;
    }
    if (!files[0])
        return;  // cancelled
    auto* editor = static_cast<Editor*>(userdata);
    const std::lock_guard lock(editor->m_dialogMutex);
    editor->m_dialogResult = DialogResult{save, files[0]};
}

void Editor::showOpenDialog()
{
    SDL_ShowOpenFileDialog([](void* editor, const char* const* files, int) { onDialog(editor, files, false); }, this,
                           m_window, &SceneFilter, 1, resourcePath("assets/scenes").c_str(), false);
}

void Editor::showSaveDialog()
{
    SDL_ShowSaveFileDialog([](void* editor, const char* const* files, int) { onDialog(editor, files, true); }, this,
                           m_window, &SceneFilter, 1, resourcePath("assets/scenes").c_str());
}

void Editor::applyDialogResult(const OverlayContext& context)
{
    std::optional<DialogResult> result;
    {
        const std::lock_guard lock(m_dialogMutex);
        result.swap(m_dialogResult);
    }
    if (!result || context.playMode != PlayMode::Edit)
        return;
    if (result->save) {
        std::filesystem::path path = result->path;
        if (path.extension() != ".scene")
            path += ".scene";
        saveScene(context, path.string());
    } else {
        openScene(context, result->path);
    }
}

void Editor::setStatus(std::string message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    m_status = std::move(message);
    m_statusTime = ImGui::GetTime();
}

void Editor::updateTitle(const std::string& path)
{
    if (path == m_titlePath || !m_window)
        return;
    m_titlePath = path;
    const std::string name = path.empty() ? "Untitled" : std::filesystem::path(path).filename().string();
    SDL_SetWindowTitle(m_window, (name + " - Renderer").c_str());
}
