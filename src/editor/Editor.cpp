#include "Editor.h"

#include "editor/AssetOps.h"

#include "engine/anim/AnimAsset.h"
#include "engine/app/CameraController.h"
#include "engine/assets/AssetCache.h"
#include "engine/core/EventBus.h"
#include "engine/core/Paths.h"
#include "engine/reflect/ComponentRegistry.h"
#include "engine/render/Renderer.h"
#include "engine/render/UIRenderer.h"
#include "engine/scene/Camera.h"
#include "engine/scene/Prefab.h"
#include "engine/scene/Scene.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/scene/Transform.h"
#include "engine/ui/UiLayout.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <filesystem>

namespace fs = std::filesystem;

// Asset fields of the loaded objects that point into from, moved to to
static void rebaseReferences(Scene& scene, const ComponentRegistry& components, const std::string& from,
                             const std::string& to)
{
    entt::registry& registry = scene.registry();
    for (const auto& info : components.all()) {
        for (const FieldInfo& field : info->fields) {
            if (field.type != FieldType::String || field.asset == AssetKind::None || !field.set)
                continue;
            for (const entt::entity e : registry.view<Uuid>()) {
                if (!info->has(registry, e))
                    continue;
                void* data = info->get(registry, e);
                const std::string value = std::get<std::string>(field.get(data));
                if (pathIsUnder(value, from))
                    field.set(data, rebasePath(value, from, to));
            }
        }
    }
}

// A file path (not a ref) that follows a move of the asset or folder from
static void rebaseFilePath(std::string& path, const std::string& from, const std::string& to)
{
    if (path.empty() || !assetops::isInsideRoot(path))
        return;
    const std::string ref = assetops::toRef(path);
    if (pathIsUnder(ref, from))
        path = assetops::fromRef(rebasePath(ref, from, to)).string();
}

void populateDefaultScene(Scene& scene)
{
    entt::registry& registry = scene.registry();
    const entt::entity camera = scene.createGameObject("Main Camera");
    Transform& cameraTransform = registry.get<Transform>(camera);
    cameraTransform.setPos({0.0f, 2.0f, 10.0f});
    cameraTransform.lookAt({0.0f, 0.0f, 0.0f});
    registry.emplace<CameraComponent>(camera);

    const entt::entity light = scene.createGameObject("Directional Light");
    Transform& lightTransform = registry.get<Transform>(light);
    lightTransform.setPos({0.0f, 3.0f, 0.0f});
    lightTransform.setEulerAngles(glm::radians(glm::vec3(-50.0f, -30.0f, 0.0f)));
    registry.emplace<DirectionalLight>(light);
}

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
    m_viewer.close();
    m_viewer.release();
    m_sceneView.release();
    m_gameView.release();
    m_canvasPanel.release();
    m_thumbnails.release();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void Editor::processEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1 && !event.key.repeat)
        m_visible = !m_visible;
    if (event.type == SDL_EVENT_DROP_FILE && event.drop.data)
        m_drops.push_back({event.drop.data, event.drop.x, event.drop.y});
    ImGui_ImplSDL3_ProcessEvent(&event);
}

bool Editor::editorCameraInput() const
{
    return m_visible && !m_viewer.isOpen() && m_sceneView.hovered() && !ImGui::GetIO().WantTextInput;
}

bool Editor::gameInput() const
{
    return !m_visible || (!m_viewer.isOpen() && m_gameView.focused() && !ImGui::GetIO().WantTextInput);
}

bool Editor::gameViewRect(float& x, float& y, float& w, float& h) const
{
    if (!m_visible || m_viewer.isOpen() || m_inPrefab || !m_gameView.visible())
        return false;
    x = m_gameView.imageX();
    y = m_gameView.imageY();
    w = m_gameView.imageWidth();
    h = m_gameView.imageHeight();
    return true;
}

bool Editor::gameViewHovered() const
{
    return m_gameView.hovered() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

bool Editor::confirmQuit()
{
    if (!m_initialized)
        return true;
    m_quitAsked = true;  // answered in update(), after any unsaved changes prompt
    return false;
}

void Editor::update(const OverlayContext& context)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    if (m_quitAsked)
        m_visible = true;  // the prompt has to be seen
    if (!m_visible)
        return;

    if (m_quitAsked && m_viewer.isOpen())
        m_viewer.close();  // the unsaved changes prompt belongs to the scene UI
    if (m_viewer.isOpen()) {
        updateViewer(context);
        return;
    }
    if (m_quitAsked) {
        m_quitAsked = false;
        if (context.playMode != PlayMode::Edit)
            setPlaying(context, false);
        request(context, Pending::Quit);
    }
    handleDialogs(context);
    recordSelection(context.scene);
    updateTitle(context);
    drawMenuBar(context);
    drawDockSpace();
    importDrops(context);

    m_debugPanel.draw(context);
    m_profilerPanel.draw(context.renderer.profiler());
    m_hierarchyPanel.draw(context.scene, context.components, m_history, m_selection, m_actions);
    m_projectPanel.draw(m_thumbnails);
    const ImGuiWindow* projectWindow = ImGui::FindWindowByName(ProjectPanel::WindowName);
    m_consolePanel.draw(projectWindow ? projectWindow->DockId : 0);
    syncAssetSelection();
    m_inspectorPanel.draw(context.scene, context.components, m_selection.active(), m_history, m_selection.size(),
                          context.assets, m_thumbnails, m_selection.empty() ? m_projectPanel.selected() : std::string());
    // The tab bar only exists after the first frame, and it selects its last tab
    if (m_selectSceneTab && ImGui::GetFrameCount() > 1) {
        m_sceneView.focus();
        m_selectSceneTab = false;
    }
    m_sceneView.setPrefab(m_inPrefab ? sceneName(context) : std::string(), m_inPrefab && sceneDirty(context));
    m_sceneView.draw(context, m_history, m_selection, m_actions, m_gameView.aspect());
    m_gameView.draw(m_inPrefab ? "Editing a prefab: close it to see the game"
                    : context.scene.gameCamera() == entt::null ? "No camera in the scene"
                                                               : nullptr);
    m_canvasPanel.draw(context, m_history, m_selection);
    m_dialogs.draw();

    if (m_sceneView.takeSaveRequest())
        saveScene(context, context.scene.path());
    if (m_sceneView.takeBackRequest())
        request(context, Pending::ClosePrefab);
    handleProjectEvents(context);
    handleInspectorRequests(context);
    if (!context.controller.isLooking())
        handleObjectShortcuts();
    EditContext edit{context.scene, context.components};
    m_actions.setPrefabMode(prefabRoot(context.scene),
                            m_inPrefab ? assetops::toRef(context.scene.path()) : std::string());
    m_actions.setAssets(&context.assets);
    m_actions.apply(edit, m_history, m_selection, spawnPoint(context));
    if (m_actions.takeCreatedUi())
        m_canvasPanel.focus();
    syncSelectedTag(context.scene.registry());
}

// Selecting an asset deselects the scene's objects, and the other way round, like Unity
void Editor::syncAssetSelection()
{
    const std::string& asset = m_projectPanel.selected();
    if (asset != m_lastAsset) {
        if (!asset.empty())
            m_selection.clear();
        m_lastAsset = asset;
    } else if (!asset.empty() && !m_selection.empty()) {
        m_projectPanel.clearSelection();
        m_lastAsset.clear();
    }
}

// Only while the hierarchy or the Scene view has the user's attention, and no text field does
void Editor::handleObjectShortcuts()
{
    if (m_projectPanel.focused() || !(m_hierarchyPanel.focused() || m_sceneView.focused() || m_sceneView.hovered()) ||
        ImGui::GetIO().WantTextInput)
        return;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, ImGuiInputFlags_RouteGlobal))
        m_actions.duplicate();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, ImGuiInputFlags_RouteGlobal))
        m_actions.copy();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V, ImGuiInputFlags_RouteGlobal))
        m_actions.paste();
    if (ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteGlobal))
        m_actions.remove();
}

// New top-level objects go a little in front of the editor camera
glm::vec3 Editor::spawnPoint(const OverlayContext& context) const
{
    const Transform& camera = context.camera.getTransform();
    return camera.getPos() + camera.forward() * 8.0f;
}

void Editor::syncSelectedTag(entt::registry& registry)
{
    m_selection.prune(registry);
    if (m_selection.items() == m_tagged)
        return;
    for (const entt::entity e : m_tagged) {
        if (registry.valid(e))
            registry.remove<Selected>(e);
    }
    for (const entt::entity e : m_selection.items())
        registry.emplace_or_replace<Selected>(e);
    m_tagged = m_selection.items();
}

void Editor::renderViews(const OverlayContext& context)
{
    if (m_viewer.isOpen()) {
        m_viewer.render(context);
        m_thumbnails.process(context.renderer, context.assets, 4);
        return;
    }
    // A prefab without a sun of its own is lit by a default one, like a new scene's
    auto suns = context.scene.registry().view<DirectionalLight>();
    if (m_inPrefab && suns.begin() == suns.end()) {
        Transform sun;
        sun.setEulerAngles(glm::radians(glm::vec3(-50.0f, -30.0f, 0.0f)));
        const DirectionalLight light;
        context.renderer.setDirectionalLight(glm::vec3(sun.getMatrix()[2]), light.color, light.intensity);
    }
    m_sceneView.render(context, m_selection);
    syncSelectedTag(context.scene.registry());
    if (m_gameView.visible()) {
        GpuScope scope(context.renderer.profiler(), "Game View");
        const RenderTarget& target = m_gameView.target();
        if (const auto view = context.scene.gameCameraView(m_gameView.aspect()))
            context.renderer.render(*view, target);
        GpuScope uiScope(context.renderer.profiler(), "UI");
        const glm::vec2 size(static_cast<float>(target.width()), static_cast<float>(target.height()));
        uiLayoutSystem(context.scene, size);
        context.ui.begin(size);
        context.ui.drawCanvases(context.scene);
        context.ui.end(&target, target.width(), target.height());
    }
    m_canvasPanel.render(context);  // last: the UIRects stay laid out for its preview
    m_thumbnails.process(context.renderer, context.assets, 4);
}

void Editor::render()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// Builds the default layout the first time (no imgui.ini entry yet) and on Reset Layout
void Editor::drawDockSpace()
{
    const ImGuiID dockId = ImGui::GetID("EditorDock");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // Layouts saved before the Scene view's window was renamed don't place it
    if (!m_layoutChecked) {
        m_layoutChecked = true;
        if (ImGui::DockBuilderGetNode(dockId) && !ImGui::FindWindowSettingsByID(ImHashStr(m_sceneView.name())))
            m_resetLayout = true;
    }
    if (m_resetLayout) {
        ImGui::DockBuilderRemoveNode(dockId);
        m_resetLayout = false;
    }
    if (!ImGui::DockBuilderGetNode(dockId)) {
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, viewport->WorkSize);
        ImGuiID center = dockId;
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
        const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.6f, nullptr, &right);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.32f, nullptr, &center);
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.2f, nullptr, &center);
        ImGui::DockBuilderDockWindow(m_gameView.name(), center);
        ImGui::DockBuilderDockWindow(m_sceneView.name(), center);
        ImGui::DockBuilderDockWindow(CanvasPanel::WindowName, center);
        ImGui::DockBuilderDockWindow("Scene Hierarchy", right);
        ImGui::DockBuilderDockWindow("Inspector", rightBottom);
        ImGui::DockBuilderDockWindow(ProjectPanel::WindowName, bottom);
        ImGui::DockBuilderDockWindow(ConsolePanel::WindowName, bottom);
        ImGui::DockBuilderDockWindow("Debug", left);
        ImGui::DockBuilderFinish(dockId);
        m_selectSceneTab = true;
    }
    ImGui::DockSpaceOverViewport(dockId, viewport);
}

void Editor::drawMenuBar(const OverlayContext& context)
{
    // The viewer leaves the scene alone: no scene commands, no scene undo
    const bool viewer = m_viewer.isOpen();
    const bool editing = context.playMode == PlayMode::Edit && !viewer;
    if (editing) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal))
            request(context, Pending::NewScene);
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal))
            m_dialogs.openOpenScene();
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
            saveScene(context, context.scene.path());
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal) && !m_inPrefab)
            m_dialogs.openSaveAs(context.scene.path());
    }

    // Global route: a focused text field keeps its own Ctrl+Z
    if (!viewer && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
        undo(context);
    if (!viewer && (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal) ||
                    ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)))
        redo(context);

    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File")) {
        ImGui::BeginDisabled(!editing);
        if (ImGui::MenuItem("New Scene", "Ctrl+N"))
            request(context, Pending::NewScene);
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
            m_dialogs.openOpenScene();
        if (ImGui::MenuItem(m_inPrefab ? "Save Prefab" : "Save", "Ctrl+S"))
            saveScene(context, context.scene.path());
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, !m_inPrefab))
            m_dialogs.openSaveAs(context.scene.path());
        if (m_inPrefab && ImGui::MenuItem("Close Prefab"))
            request(context, Pending::ClosePrefab);
        ImGui::EndDisabled();
        if (viewer) {
            if (ImGui::MenuItem("Close Animation Viewer", "Esc"))
                m_viewer.close();
        } else if (!editing) {
            ImGui::TextDisabled("Stop playing to save or load");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            m_quitAsked = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit", !viewer)) {
        const std::string undoLabel = m_history.canUndo() ? "Undo " + m_history.undoName() : "Undo";
        const std::string redoLabel = m_history.canRedo() ? "Redo " + m_history.redoName() : "Redo";
        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, m_history.canUndo()))
            undo(context);
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, m_history.canRedo()))
            redo(context);
        ImGui::Separator();
        m_actions.drawEditItems(!m_selection.empty());
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Assets", !viewer)) {
        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Folder"))
                m_projectPanel.createFolder();
            ImGui::Separator();
            if (ImGui::MenuItem("Scene"))
                m_projectPanel.createScene();
            if (ImGui::MenuItem("Material"))
                m_projectPanel.createMaterial();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Show in File Manager"))
            m_projectPanel.showInFileManager(m_projectPanel.currentFolder());
        if (ImGui::MenuItem("Refresh"))
            m_projectPanel.refresh();
        ImGui::Separator();
        ImGui::TextDisabled("Drop files on the window to import them");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("GameObject", !viewer)) {
        m_actions.drawCreateItems();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Profiler", nullptr, &m_profilerPanel.open());
        ImGui::MenuItem("Console", nullptr, &m_consolePanel.open());
        if (ImGui::MenuItem("Canvas", nullptr, m_canvasPanel.open()))
            m_canvasPanel.focus();
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout"))
            m_resetLayout = true;
        ImGui::EndMenu();
    }

    drawPlayButtons(context);

    const char* modeText = context.playMode == PlayMode::Edit   ? "Edit"
                           : context.playMode == PlayMode::Play ? "Playing"
                                                                : "Paused";
    std::string text = (m_inPrefab ? "Prefab: " : "") + sceneName(context) + (sceneDirty(context) ? "*" : "") + "  |  " + modeText;
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
    ImGui::BeginDisabled(m_inPrefab || m_viewer.isOpen());
    const bool toggled = ImGui::Button(playing ? "Stop" : "Play");
    ImGui::EndDisabled();
    if (m_inPrefab)
        ImGui::SetItemTooltip("Close the prefab to play");
    else if (m_viewer.isOpen())
        ImGui::SetItemTooltip("Close the Animation Viewer to play");
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
// Uuids survive the round trip, so the selection and the edit history stay valid.
// Edits made while playing go to a separate history that Stop throws away.
void Editor::setPlaying(const OverlayContext& context, bool playing)
{
    if (playing && m_inPrefab)
        return;
    if (playing) {
        m_playSnapshot = ::saveScene(context.scene, context.components);
        m_playSelection = selectionUuids(context.scene);
        m_editHistory = std::move(m_history);
        m_history.clear();
        context.playMode = PlayMode::Play;
        m_gameView.focus();
        return;
    }

    context.playMode = PlayMode::Edit;
    if (EventBus* bus = context.scene.registry().ctx().find<EventBus>())
        bus->clearQueue();
    std::string error;
    if (loadScene(context.scene, context.components, m_playSnapshot, &error))
        setSelection(context.scene, m_playSelection);
    else
        setStatus("Restore failed: " + error);
    m_lastSelection = selectionUuids(context.scene);
    m_history = std::move(m_editHistory);
    m_editHistory.clear();
    m_playSnapshot.clear();
    m_sceneView.focus();
}

void Editor::undo(const OverlayContext& context)
{
    EditContext edit{context.scene, context.components};
    if (const auto selection = m_history.undo(edit))
        setSelection(context.scene, *selection);
}

void Editor::redo(const OverlayContext& context)
{
    EditContext edit{context.scene, context.components};
    if (const auto selection = m_history.redo(edit))
        setSelection(context.scene, *selection);
}

SelectionIds Editor::selectionUuids(const Scene& scene) const
{
    SelectionIds uuids;
    for (const entt::entity e : m_selection.items()) {
        if (scene.registry().valid(e))
            uuids.push_back(scene.uuidOf(e));
    }
    return uuids;
}

// Restoring a selection is not a selection change of its own
void Editor::setSelection(const Scene& scene, const SelectionIds& uuids)
{
    std::vector<entt::entity> items;
    for (const std::uint64_t uuid : uuids) {
        const entt::entity e = uuid == 0 ? entt::null : scene.findByUuid(uuid);
        if (e != entt::null && scene.registry().valid(e))
            items.push_back(e);
    }
    m_selection.assign(std::move(items));
    m_lastSelection = selectionUuids(scene);
}

// Runs at the start of a frame, so it also sees picks made while rendering the last one
void Editor::recordSelection(const Scene& scene)
{
    m_selection.prune(scene.registry());
    SelectionIds now = selectionUuids(scene);
    std::string name = "Clear Selection";
    if (now.size() == 1) {
        const Name* label = scene.registry().try_get<Name>(m_selection.active());
        name = "Select " + (label && !label->value.empty() ? label->value : std::string("Object"));
    } else if (now.size() > 1) {
        name = "Select " + std::to_string(now.size()) + " Objects";
    }
    m_history.endFrame(m_lastSelection, now, std::move(name));
    m_lastSelection = std::move(now);
}

bool Editor::sceneDirty(const OverlayContext& context) const
{
    const EditHistory& history = context.playMode == PlayMode::Edit ? m_history : m_editHistory;
    return m_forceDirty || history.stateId() != m_savedState;
}

void Editor::markClean()
{
    m_savedState = m_history.stateId();
    m_forceDirty = false;
}

std::string Editor::sceneName(const OverlayContext& context) const
{
    const std::string& path = context.scene.path();
    return path.empty() ? std::string("Untitled") : fs::path(path).stem().string();
}

void Editor::request(const OverlayContext& context, Pending action, std::string sceneRef)
{
    if (action != Pending::Quit && context.playMode != PlayMode::Edit) {
        setStatus("Stop playing to change scenes or open prefabs");
        return;
    }
    if (action == Pending::ClosePrefab && !m_inPrefab)
        return;
    if (action == Pending::OpenPrefab && m_inPrefab &&
        assetops::toRef(context.scene.path()) == sceneRef) {
        m_sceneView.focus();
        return;
    }
    m_pending = action;
    m_pendingScene = std::move(sceneRef);
    m_pendingAfterSaveAs = false;
    // Opening a prefab from the scene parks the scene, nothing is lost
    const bool losesChanges = action != Pending::OpenPrefab || m_inPrefab;
    if (losesChanges && sceneDirty(context))
        m_dialogs.openUnsaved(sceneName(context));
    else
        runPending(context);
}

void Editor::runPending(const OverlayContext& context)
{
    const Pending action = std::exchange(m_pending, Pending::None);
    std::string ref = std::exchange(m_pendingScene, {});
    m_pendingAfterSaveAs = false;
    // The prefab's changes are settled; the scene's come next
    if (m_inPrefab && (action == Pending::NewScene || action == Pending::OpenScene || action == Pending::Quit)) {
        closePrefab(context);
        request(context, action, std::move(ref));
        return;
    }
    switch (action) {
    case Pending::NewScene:
        newScene(context);
        break;
    case Pending::OpenScene:
        openScene(context, assetops::fromRef(ref).string());
        break;
    case Pending::OpenPrefab:
        if (m_inPrefab)
            closePrefab(context);
        openPrefab(context, ref);
        break;
    case Pending::ClosePrefab:
        closePrefab(context);
        break;
    case Pending::Quit:
        m_quitNow = true;
        break;
    case Pending::None:
        break;
    }
}

void Editor::handleDialogs(const OverlayContext& context)
{
    if (const auto path = m_dialogs.takeSavePath()) {
        const bool saved = context.playMode == PlayMode::Edit && saveScene(context, path->string());
        if (m_pendingAfterSaveAs) {
            if (saved)
                runPending(context);
            else
                m_pending = Pending::None;
        }
    }
    if (m_dialogs.takeSaveAsCancelled() && m_pendingAfterSaveAs) {
        m_pending = Pending::None;
        m_pendingAfterSaveAs = false;
    }
    if (const auto ref = m_dialogs.takeOpenRef())
        request(context, Pending::OpenScene, *ref);

    switch (m_dialogs.takeUnsavedChoice()) {
    case SceneDialogs::Choice::Save:
        if (context.scene.path().empty()) {
            m_pendingAfterSaveAs = true;
            m_dialogs.openSaveAs("");
        } else if (saveScene(context, context.scene.path())) {
            runPending(context);
        } else {
            m_pending = Pending::None;
        }
        break;
    case SceneDialogs::Choice::Discard:
        runPending(context);
        break;
    case SceneDialogs::Choice::Cancel:
        m_pending = Pending::None;
        break;
    case SceneDialogs::Choice::None:
        break;
    }
}

void Editor::newScene(const OverlayContext& context)
{
    Scene& scene = context.scene;
    scene.clear();
    scene.setPath("");
    m_selection.clear();
    m_lastSelection.clear();
    m_history.clear();
    populateDefaultScene(scene);
    markClean();
}

void Editor::openScene(const OverlayContext& context, const std::string& path)
{
    std::string error;
    if (loadSceneFile(context.scene, context.components, path, &error)) {
        m_selection.clear();
        m_lastSelection.clear();
        m_history.clear();
        markClean();
        setStatus("Opened " + fs::path(path).filename().string());
        if (const int changed = refreshPrefabInstances(context.scene, context.components); changed > 0) {
            m_forceDirty = true;
            setStatus("Updated " + std::to_string(changed) + " prefab instance" + (changed == 1 ? "" : "s") +
                      " in " + fs::path(path).filename().string());
        }
    } else {
        setStatus("Open failed: " + error);
    }
}

bool Editor::saveScene(const OverlayContext& context, const std::string& path)
{
    if (m_inPrefab)
        return savePrefab(context);
    if (path.empty()) {
        m_dialogs.openSaveAs("");
        return false;
    }
    std::string error;
    if (!saveSceneFile(context.scene, context.components, path, &error)) {
        setStatus("Save failed: " + error);
        return false;
    }
    markClean();
    m_projectPanel.refresh();
    setStatus("Saved " + assetops::displayPath(assetops::toRef(path)));
    return true;
}

entt::entity Editor::prefabRoot(const Scene& scene) const
{
    return m_inPrefab ? scene.registry().get<Relationship>(scene.root()).firstChild : entt::null;
}

// The scene is parked like Play parks it; the prefab's tree keeps its uuids, so saving
// writes back the same objects
void Editor::openPrefab(const OverlayContext& context, const std::string& ref)
{
    if (context.playMode != PlayMode::Edit) {
        setStatus("Stop playing to open prefabs");
        return;
    }
    const fs::path file = assetops::fromRef(ref);
    const auto text = assetops::readText(file);
    {
        Scene probe;
        if (!text || loadEntityTree(probe, context.components, *text) == entt::null) {
            setStatus("Can't open prefab " + assetops::displayPath(ref));
            return;
        }
    }

    Scene& scene = context.scene;
    m_parked.snapshot = ::saveScene(scene, context.components);
    m_parked.path = scene.path();
    m_parked.history = std::move(m_history);
    m_history.clear();
    m_parked.savedState = m_savedState;
    m_parked.forceDirty = m_forceDirty;
    m_parked.selection = selectionUuids(scene);
    m_parked.camera = context.camera.getTransform();
    m_parked.moves.clear();

    scene.clear();
    const entt::entity root = loadEntityTree(scene, context.components, *text);
    scene.setPath(file.string());
    m_inPrefab = true;
    m_selection.clear();
    m_lastSelection.clear();
    markClean();
    scene.registry().remove<PrefabInstance>(root);
    m_forceDirty = refreshPrefabInstances(scene, context.components) > 0;  // nested prefabs that changed

    // Frame the prefab: its root and how far its objects reach from it
    scene.updateWorldTransforms();
    const glm::vec3 center(scene.registry().get<WorldTransform>(root).matrix[3]);
    float radius = 1.0f;
    for (auto [e, world] : scene.registry().view<WorldTransform>().each())
        radius = std::max(radius, glm::length(glm::vec3(world.matrix[3]) - center) + 1.0f);
    Transform& camera = context.camera.getTransform();
    camera.setPos(center + glm::normalize(glm::vec3(0.0f, 0.5f, 1.0f)) * (radius * 2.5f + 2.0f));
    camera.lookAt(center);
    context.controller.syncFrom(context.camera);

    m_sceneView.focus();
    setStatus("Opened prefab " + assetops::displayPath(ref));
}

// Back to the scene as it was parked; the prefab's unsaved changes are dropped
void Editor::closePrefab(const OverlayContext& context)
{
    if (!m_inPrefab)
        return;
    Scene& scene = context.scene;
    std::string error;
    if (!loadScene(scene, context.components, m_parked.snapshot, &error))
        setStatus("Restore failed: " + error);
    scene.setPath(m_parked.path);
    m_inPrefab = false;
    for (const auto& [from, to] : m_parked.moves)
        rebaseReferences(scene, context.components, from, to);
    m_history = std::move(m_parked.history);
    m_savedState = m_parked.savedState;
    m_forceDirty = m_parked.forceDirty;
    int changed = 0;
    for (const std::string& ref : m_parked.savedPrefabs)
        changed += refreshPrefabInstances(scene, context.components, ref);
    if (changed > 0) {
        m_forceDirty = true;
        setStatus("Updated " + std::to_string(changed) + " prefab instance" + (changed == 1 ? "" : "s"));
    }
    setSelection(scene, m_parked.selection);
    context.camera.getTransform() = m_parked.camera;
    context.controller.syncFrom(context.camera);
    m_parked = ParkedScene{};
    m_sceneView.focus();
}

// A prefab is one tree: everything has to hang under a single root object
bool Editor::savePrefab(const OverlayContext& context)
{
    Scene& scene = context.scene;
    entt::entity root = entt::null;
    int roots = 0;
    scene.forEachChild(scene.root(), [&](entt::entity e) {
        if (roots++ == 0)
            root = e;
    });
    if (roots != 1) {
        setStatus(roots == 0 ? "Can't save an empty prefab" : "A prefab has one root object: move the others under it");
        return false;
    }
    std::string error;
    if (!assetops::writeText(scene.path(), saveEntityTree(scene, context.components, root) + "\n", &error)) {
        setStatus("Save failed: " + error);
        return false;
    }
    markClean();
    const std::string ref = assetops::toRef(scene.path());
    if (std::find(m_parked.savedPrefabs.begin(), m_parked.savedPrefabs.end(), ref) == m_parked.savedPrefabs.end())
        m_parked.savedPrefabs.push_back(ref);
    m_thumbnails.invalidate(ref);
    m_projectPanel.refresh();
    setStatus("Saved " + assetops::displayPath(ref));
    return true;
}

void Editor::handleProjectEvents(const OverlayContext& context)
{
    ProjectPanel::Events events = m_projectPanel.takeEvents();
    for (std::string& error : events.errors)
        setStatus(std::move(error));
    for (const auto& [from, to] : events.moved)
        onAssetMoved(context, from, to);
    for (const std::string& ref : events.deleted)
        onAssetDeleted(context, ref);
    if (events.openScene)
        request(context, Pending::OpenScene, *events.openScene);
    if (events.openPrefab)
        request(context, Pending::OpenPrefab, *events.openPrefab);
    if (events.instantiate)
        m_actions.instantiate(*events.instantiate);
    if (events.openAnimationViewer)
        openAnimationViewer(context, *events.openAnimationViewer, {}, {});
    if (events.createScene)
        createSceneAsset(context, *events.createScene);
    if (events.createPrefab)
        createPrefab(context, events.createPrefab->first, events.createPrefab->second);
}

void Editor::handleInspectorRequests(const OverlayContext& context)
{
    InspectorPanel::Requests requests = m_inspectorPanel.takeRequests();
    for (std::string& error : requests.errors)
        setStatus(std::move(error));
    if (!requests.ping.empty())
        m_projectPanel.ping(requests.ping);
    if (!requests.openScene.empty())
        request(context, Pending::OpenScene, requests.openScene);
    if (!requests.openPrefab.empty())
        request(context, Pending::OpenPrefab, requests.openPrefab);
    if (!requests.instantiate.empty())
        m_actions.instantiate(requests.instantiate);
    if (requests.editCanvas)
        m_canvasPanel.focus();
    if (requests.openAnimationViewer)
        openAnimationViewer(context, requests.openAnimationViewer->model, requests.openAnimationViewer->material,
                            requests.openAnimationViewer->clip);
}

void Editor::openAnimationViewer(const OverlayContext& context, const std::string& model, const std::string& material,
                                 const std::string& clip)
{
    if (context.playMode != PlayMode::Edit) {
        setStatus("Stop playing to open the Animation Viewer");
        return;
    }
    // An .anim or .pose opens the model it was made on, then itself
    const AssetKind kind = assetKindOf(model);
    if (kind == AssetKind::Animation || kind == AssetKind::Pose) {
        const auto text = assetops::readText(assetops::fromRef(model));
        const std::optional<AnimHeader> header = text ? readAnimHeader(*text) : std::nullopt;
        if (!header || header->model.empty()) {
            setStatus("Can't tell which model " + assetops::displayPath(model) + " belongs to");
            return;
        }
        std::string error;
        if (!m_viewer.open(context.assets, context.renderer, header->model, {}, {}, &error)) {
            setStatus("Can't open the Animation Viewer: " + error);
            return;
        }
        m_viewer.openAsset(context.assets, model);
        return;
    }
    std::string error;
    if (!m_viewer.open(context.assets, context.renderer, model, material, clip, &error))
        setStatus("Can't open the Animation Viewer: " + error);
}

// The viewer's frame: menu bar, its own dock space and panels; the scene's panels wait
void Editor::updateViewer(const OverlayContext& context)
{
    updateTitle(context);
    drawMenuBar(context);
    m_profilerPanel.draw(context.renderer.profiler());
    m_consolePanel.draw(0);
    m_viewer.draw(context);
    m_dialogs.draw();
    if (m_viewer.takeAssetsChanged())
        m_projectPanel.refresh();
    if (m_viewer.takeCloseRequest())
        m_viewer.close();
    if (!m_viewer.isOpen())
        m_sceneView.focus();
}

// OS file drops: copied into the open Project folder. A model dropped on the Scene view is
// also placed where it landed.
void Editor::importDrops(const OverlayContext& context)
{
    if (m_drops.empty())
        return;
    const fs::path folder = m_projectPanel.currentFolder();
    int imported = 0;
    std::string last;
    for (const Drop& drop : m_drops) {
        const fs::path source(drop.path);
        std::string ref;
        if (assetops::isInsideRoot(source)) {
            ref = assetops::toRef(source);  // already part of the project
        } else {
            std::string error;
            const auto copy = assetops::import(source, folder, &error);
            if (!copy) {
                setStatus("Import failed: " + error);
                continue;
            }
            ref = assetops::toRef(*copy);
            ++imported;
        }
        last = ref;
        const AssetKind kind = assetKindOf(ref);
        if ((kind == AssetKind::Model || kind == AssetKind::Prefab) && context.playMode == PlayMode::Edit &&
            m_sceneView.imageContains(drop.x, drop.y))
            m_actions.instantiate(ref, entt::null, SceneActions::Placement::At,
                                  m_sceneView.dropPointAt(context, drop.x, drop.y));
    }
    m_drops.clear();
    m_projectPanel.refresh();
    if (!last.empty())
        m_projectPanel.ping(last);
    if (imported > 0)
        setStatus("Imported " + std::to_string(imported) + (imported == 1 ? " file" : " files") + " into " +
                  assetops::displayPath(assetops::toRef(folder)));
}

// References follow a moved file or folder: loaded assets, the open scene, and every
// scene, prefab and material on disk
void Editor::onAssetMoved(const OverlayContext& context, const std::string& from, const std::string& to)
{
    context.assets.rename(from, to);
    m_thumbnails.invalidate(to);

    rebaseReferences(context.scene, context.components, from, to);
    const int files = assetops::rewriteReferences(from, to);

    std::string scenePath = context.scene.path();
    rebaseFilePath(scenePath, from, to);
    context.scene.setPath(std::move(scenePath));
    if (m_inPrefab) {
        m_parked.moves.emplace_back(from, to);
        rebaseFilePath(m_parked.path, from, to);
    }
    if (files > 0)
        setStatus("Updated references in " + std::to_string(files) + (files == 1 ? " file" : " files"));
}

void Editor::onAssetDeleted(const OverlayContext& context, const std::string& ref)
{
    m_thumbnails.invalidate(ref);
    auto deleted = [&](const std::string& path) {
        return !path.empty() && assetops::isInsideRoot(path) && pathIsUnder(assetops::toRef(path), ref);
    };
    if (m_inPrefab) {
        if (deleted(m_parked.path)) {
            m_parked.path.clear();
            m_parked.forceDirty = true;
        }
        if (deleted(context.scene.path())) {
            closePrefab(context);
            setStatus("The open prefab was deleted");
        }
    } else if (deleted(context.scene.path())) {
        context.scene.setPath("");
        m_forceDirty = true;
    }
}

void Editor::createSceneAsset(const OverlayContext& context, const fs::path& folder)
{
    Scene scene;
    populateDefaultScene(scene);
    const fs::path path = assetops::uniquePath(folder, "New Scene", ".scene");
    std::string error;
    if (!assetops::writeText(path, ::saveScene(scene, context.components) + "\n", &error)) {
        setStatus("Can't create scene: " + error);
        return;
    }
    m_projectPanel.refresh();
    m_projectPanel.beginRename(assetops::toRef(path));
}

// A prefab is the object's tree in the scene format; instantiating it adds a copy
void Editor::createPrefab(const OverlayContext& context, entt::entity e, const fs::path& folder)
{
    Scene& scene = context.scene;
    if (!scene.registry().valid(e))
        return;
    const Name* name = scene.registry().try_get<Name>(e);
    std::string stem = name && assetops::validName(name->value) ? name->value : std::string("Prefab");
    const fs::path path = assetops::uniquePath(folder, stem, ".prefab");
    // Saved without its link to a prefab it may be an instance of; then it becomes an
    // instance of the new one
    entt::registry& registry = scene.registry();
    std::optional<PrefabInstance> link;
    if (const PrefabInstance* instance = registry.try_get<PrefabInstance>(e))
        link = *instance;
    registry.remove<PrefabInstance>(e);
    const std::string text = saveEntityTree(scene, context.components, e);
    std::string error;
    if (!assetops::writeText(path, text + "\n", &error)) {
        if (link)
            registry.emplace<PrefabInstance>(e, *link);
        setStatus("Can't create prefab: " + error);
        return;
    }
    const std::string ref = assetops::toRef(path);
    registry.emplace<PrefabInstance>(e, ref);
    m_forceDirty = true;
    m_projectPanel.refresh();
    m_projectPanel.ping(ref);
    setStatus("Created " + assetops::displayPath(ref));
}

void Editor::setStatus(std::string message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    m_status = std::move(message);
    m_statusTime = ImGui::GetTime();
}

void Editor::updateTitle(const OverlayContext& context)
{
    const std::string title = sceneName(context) + (sceneDirty(context) ? "*" : "") + " - Renderer";
    if (title == m_title || !m_window)
        return;
    m_title = title;
    SDL_SetWindowTitle(m_window, title.c_str());
}
