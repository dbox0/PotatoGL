#pragma once

#include "engine/app/Overlay.h"
#include "editor/AnimationViewer.h"
#include "editor/CanvasPanel.h"
#include "editor/ConsolePanel.h"
#include "editor/DebugPanel.h"
#include "editor/EditHistory.h"
#include "editor/InspectorPanel.h"
#include "editor/ProfilerPanel.h"
#include "editor/ProjectPanel.h"
#include "editor/SceneActions.h"
#include "editor/SceneDialogs.h"
#include "editor/SceneHierarchyPanel.h"
#include "editor/SceneViewPanel.h"
#include "editor/Selection.h"
#include "editor/ThumbnailCache.h"
#include "editor/ViewportPanel.h"

#include "engine/scene/Transform.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// Dear ImGui docking editor
// The only place that touches ImGui.
class Editor final : public Overlay
{
private:
    bool m_initialized = false;
    bool m_visible = true;
    DebugPanel m_debugPanel;
    SceneHierarchyPanel m_hierarchyPanel;
    InspectorPanel m_inspectorPanel;
    ProfilerPanel m_profilerPanel;
    ConsolePanel m_consolePanel;
    ProjectPanel m_projectPanel;
    SceneViewPanel m_sceneView;
    ViewportPanel m_gameView{"Game"};
    CanvasPanel m_canvasPanel;
    ThumbnailCache m_thumbnails;
    AnimationViewer m_viewer;  // while open it replaces the scene UI
    SceneDialogs m_dialogs;
    bool m_selectSceneTab = true;  // the Scene view is the tab shown at startup
    bool m_resetLayout = false;
    bool m_layoutChecked = false;

    Selection m_selection;
    std::vector<entt::entity> m_tagged;  // carry the Selected tag
    SceneActions m_actions;
    std::string m_lastAsset;  // Project selection last frame; objects and assets aren't selected together

    SDL_Window* m_window = nullptr;

    EditHistory m_history;
    EditHistory m_editHistory;          // the Edit-mode history, parked while playing
    std::string m_playSnapshot;         // scene as it was when Play was pressed
    SelectionIds m_playSelection;       // the selection when Play was pressed
    SelectionIds m_lastSelection;       // the selection at the last recordSelection()
    std::string m_status;               // last message, shown in the menu bar
    double m_statusTime = 0.0;
    std::string m_title = "\x01";       // window title last set

    // Unsaved changes: the history's state at the last save or load
    std::uint64_t m_savedState = 0;
    bool m_forceDirty = false;  // changed outside the history (e.g. its file was deleted)

    // Prefab mode: the open prefab is loaded into the scene, whose path is the prefab's file,
    // and the scene waits here until the prefab is closed
    struct ParkedScene
    {
        std::string snapshot;
        std::string path;
        EditHistory history;
        std::uint64_t savedState = 0;
        bool forceDirty = false;
        SelectionIds selection;
        Transform camera;
        std::vector<std::pair<std::string, std::string>> moves;  // asset moves to apply on return
        std::vector<std::string> savedPrefabs;  // refs; their instances are rebuilt on return
    };
    bool m_inPrefab = false;
    ParkedScene m_parked;

    // What waits for the unsaved changes prompt (and maybe Save As) to be answered
    enum class Pending
    {
        None,
        NewScene,
        OpenScene,
        OpenPrefab,
        ClosePrefab,
        Quit,
    };
    Pending m_pending = Pending::None;
    std::string m_pendingScene;  // ref, for OpenScene and OpenPrefab
    bool m_pendingAfterSaveAs = false;
    bool m_quitAsked = false;
    bool m_quitNow = false;

    // Files dropped on the window by the OS, imported at the next update()
    struct Drop
    {
        std::string path;
        float x, y;
    };
    std::vector<Drop> m_drops;

    void syncSelectedTag(entt::registry& registry);
    void syncAssetSelection();
    void handleObjectShortcuts();
    glm::vec3 spawnPoint(const OverlayContext& context) const;

    void drawDockSpace();
    void drawMenuBar(const OverlayContext& context);
    void drawPlayButtons(const OverlayContext& context);
    void setPlaying(const OverlayContext& context, bool playing);
    void undo(const OverlayContext& context);
    void redo(const OverlayContext& context);
    SelectionIds selectionUuids(const Scene& scene) const;
    void setSelection(const Scene& scene, const SelectionIds& uuids);
    void recordSelection(const Scene& scene);

    bool sceneDirty(const OverlayContext& context) const;
    void markClean();
    std::string sceneName(const OverlayContext& context) const;
    // Asks about unsaved changes first if there are any
    void request(const OverlayContext& context, Pending action, std::string sceneRef = {});
    void runPending(const OverlayContext& context);
    void handleDialogs(const OverlayContext& context);

    void newScene(const OverlayContext& context);
    void openScene(const OverlayContext& context, const std::string& path);
    // Empty path: asks for one with Save As. True if it was written.
    bool saveScene(const OverlayContext& context, const std::string& path);

    void openPrefab(const OverlayContext& context, const std::string& ref);
    void closePrefab(const OverlayContext& context);
    bool savePrefab(const OverlayContext& context);
    // The open prefab's root object; entt::null outside prefab mode
    entt::entity prefabRoot(const Scene& scene) const;

    void handleProjectEvents(const OverlayContext& context);
    void handleInspectorRequests(const OverlayContext& context);
    void importDrops(const OverlayContext& context);
    void onAssetMoved(const OverlayContext& context, const std::string& from, const std::string& to);
    void onAssetDeleted(const OverlayContext& context, const std::string& ref);
    void createSceneAsset(const OverlayContext& context, const std::filesystem::path& folder);
    void createPrefab(const OverlayContext& context, entt::entity e, const std::filesystem::path& folder);

    void openAnimationViewer(const OverlayContext& context, const std::string& model, const std::string& material,
                             const std::string& clip);
    void updateViewer(const OverlayContext& context);

    void setStatus(std::string message);
    void updateTitle(const OverlayContext& context);

public:
    Editor() = default;
    ~Editor() override;

    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    bool init(SDL_Window* window, SDL_GLContext context) override;
    void shutdown() override;

    void processEvent(const SDL_Event& event) override;
    bool editorCameraInput() const override;
    bool gameInput() const override;
    bool visible() const override { return m_visible; }
    bool gameViewRect(float& x, float& y, float& w, float& h) const override;
    bool gameViewHovered() const override;
    Scene* activeScene() override { return m_viewer.isOpen() ? m_viewer.scene() : nullptr; }
    bool confirmQuit() override;
    bool quitRequested() const override { return m_quitNow; }

    void update(const OverlayContext& context) override;
    void renderViews(const OverlayContext& context) override;
    void render() override;
};

// A new scene's contents: a camera and a directional light
void populateDefaultScene(Scene& scene);
