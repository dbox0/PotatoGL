#include "Editor.h"

#include "editor/AssetOps.h"

#include "engine/app/CameraController.h"
#include "engine/assets/AssetCache.h"
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

namespace fs = std::filesystem;

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
    m_sceneView.release();
    m_gameView.release();
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
    return m_visible && m_sceneView.hovered() && !ImGui::GetIO().WantTextInput;
}

bool Editor::gameInput() const
{
    return !m_visible || (m_gameView.focused() && !ImGui::GetIO().WantTextInput);
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
    syncAssetSelection();
    m_inspectorPanel.draw(context.scene, context.components, m_selection.active(), m_history, m_selection.size(),
                          context.assets, m_thumbnails, m_selection.empty() ? m_projectPanel.selected() : std::string());
    // The tab bar only exists after the first frame, and it selects its last tab
    if (m_selectSceneTab && ImGui::GetFrameCount() > 1) {
        m_sceneView.focus();
        m_selectSceneTab = false;
    }
    m_sceneView.draw(context, m_history, m_selection, m_actions, m_gameView.aspect());
    m_gameView.draw(context.scene.gameCamera() == entt::null ? "No camera in the scene" : nullptr);
    m_dialogs.draw();

    handleProjectEvents(context);
    handleInspectorRequests(context);
    if (!context.controller.isLooking())
        handleObjectShortcuts();
    EditContext edit{context.scene, context.components};
    m_actions.apply(edit, m_history, m_selection, spawnPoint(context));
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
    m_sceneView.render(context, m_selection);
    syncSelectedTag(context.scene.registry());
    if (m_gameView.visible()) {
        GpuScope scope(context.renderer.profiler(), "Game View");
        if (const auto view = context.scene.gameCameraView(m_gameView.aspect()))
            context.renderer.render(*view, m_gameView.target());
    }
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
        ImGui::DockBuilderDockWindow(m_sceneView.name(), center);
        ImGui::DockBuilderDockWindow(m_gameView.name(), center);
        ImGui::DockBuilderDockWindow("Scene Hierarchy", right);
        ImGui::DockBuilderDockWindow("Inspector", rightBottom);
        ImGui::DockBuilderDockWindow(ProjectPanel::WindowName, bottom);
        ImGui::DockBuilderDockWindow("Debug", left);
        ImGui::DockBuilderFinish(dockId);
        m_selectSceneTab = true;
    }
    ImGui::DockSpaceOverViewport(dockId, viewport);
}

void Editor::drawMenuBar(const OverlayContext& context)
{
    const bool editing = context.playMode == PlayMode::Edit;
    if (editing) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal))
            request(context, Pending::NewScene);
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal))
            m_dialogs.openOpenScene();
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
            saveScene(context, context.scene.path());
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
            m_dialogs.openSaveAs(context.scene.path());
    }

    // Global route: a focused text field keeps its own Ctrl+Z
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
        undo(context);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
        redo(context);

    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File")) {
        ImGui::BeginDisabled(!editing);
        if (ImGui::MenuItem("New Scene", "Ctrl+N"))
            request(context, Pending::NewScene);
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
            m_dialogs.openOpenScene();
        if (ImGui::MenuItem("Save", "Ctrl+S"))
            saveScene(context, context.scene.path());
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
            m_dialogs.openSaveAs(context.scene.path());
        ImGui::EndDisabled();
        if (!editing)
            ImGui::TextDisabled("Stop playing to save or load");
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            m_quitAsked = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
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

    if (ImGui::BeginMenu("Assets")) {
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

    if (ImGui::BeginMenu("GameObject")) {
        m_actions.drawCreateItems();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Profiler", nullptr, &m_profilerPanel.open());
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout"))
            m_resetLayout = true;
        ImGui::EndMenu();
    }

    drawPlayButtons(context);

    const char* modeText = context.playMode == PlayMode::Edit   ? "Edit"
                           : context.playMode == PlayMode::Play ? "Playing"
                                                                : "Paused";
    std::string text = sceneName(context) + (sceneDirty(context) ? "*" : "") + "  |  " + modeText;
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
// Uuids survive the round trip, so the selection and the edit history stay valid.
// Edits made while playing go to a separate history that Stop throws away.
void Editor::setPlaying(const OverlayContext& context, bool playing)
{
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
        setStatus("Stop playing to change scenes");
        return;
    }
    m_pending = action;
    m_pendingScene = std::move(sceneRef);
    m_pendingAfterSaveAs = false;
    if (sceneDirty(context))
        m_dialogs.openUnsaved(sceneName(context));
    else
        runPending(context);
}

void Editor::runPending(const OverlayContext& context)
{
    const Pending action = std::exchange(m_pending, Pending::None);
    m_pendingAfterSaveAs = false;
    switch (action) {
    case Pending::NewScene:
        newScene(context);
        break;
    case Pending::OpenScene:
        openScene(context, assetops::fromRef(m_pendingScene).string());
        break;
    case Pending::Quit:
        m_quitNow = true;
        break;
    case Pending::None:
        break;
    }
    m_pendingScene.clear();
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
    } else {
        setStatus("Open failed: " + error);
    }
}

bool Editor::saveScene(const OverlayContext& context, const std::string& path)
{
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
    if (events.instantiate)
        m_actions.instantiate(*events.instantiate);
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
    if (!requests.instantiate.empty())
        m_actions.instantiate(requests.instantiate);
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

    entt::registry& registry = context.scene.registry();
    for (const auto& info : context.components.all()) {
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
    const int files = assetops::rewriteReferences(from, to);

    const std::string& scenePath = context.scene.path();
    if (!scenePath.empty() && assetops::isInsideRoot(scenePath)) {
        const std::string sceneRef = assetops::toRef(scenePath);
        if (pathIsUnder(sceneRef, from))
            context.scene.setPath(assetops::fromRef(rebasePath(sceneRef, from, to)).string());
    }
    if (files > 0)
        setStatus("Updated references in " + std::to_string(files) + (files == 1 ? " file" : " files"));
}

void Editor::onAssetDeleted(const OverlayContext& context, const std::string& ref)
{
    m_thumbnails.invalidate(ref);
    const std::string& scenePath = context.scene.path();
    if (!scenePath.empty() && assetops::isInsideRoot(scenePath) && pathIsUnder(assetops::toRef(scenePath), ref)) {
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
    std::string error;
    if (!assetops::writeText(path, saveEntityTree(scene, context.components, e) + "\n", &error)) {
        setStatus("Can't create prefab: " + error);
        return;
    }
    const std::string ref = assetops::toRef(path);
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
