#pragma once

#include "engine/app/Overlay.h"
#include "editor/DebugPanel.h"
#include "editor/EditHistory.h"
#include "editor/InspectorPanel.h"
#include "editor/SceneHierarchyPanel.h"
#include "editor/SceneViewPanel.h"
#include "editor/ViewportPanel.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// Dear ImGui docking editor; F1 hides it and shows the game camera fullscreen.
// The only place that touches ImGui.
class Editor final : public Overlay
{
private:
    bool m_initialized = false;
    bool m_visible = true;
    DebugPanel m_debugPanel;
    SceneHierarchyPanel m_hierarchyPanel;
    InspectorPanel m_inspectorPanel;
    SceneViewPanel m_sceneView;
    ViewportPanel m_gameView{"Game"};
    bool m_selectSceneTab = false;

    entt::entity m_selected = entt::null;
    entt::entity m_tagged = entt::null;  // carries the Selected tag

    void syncSelectedTag(entt::registry& registry);

    // File dialogs answer on another thread; the result is applied in update()
    struct DialogResult
    {
        bool save;
        std::string path;
    };
    SDL_Window* m_window = nullptr;
    std::mutex m_dialogMutex;
    std::optional<DialogResult> m_dialogResult;

    EditHistory m_history;
    EditHistory m_editHistory;          // the Edit-mode history, parked while playing
    std::string m_playSnapshot;         // scene as it was when Play was pressed
    std::uint64_t m_playSelection = 0;  // uuid of the selection when Play was pressed
    std::uint64_t m_lastSelection = 0;  // uuid of the selection at the last recordSelection()
    std::string m_status;               // last file error, shown in the menu bar
    double m_statusTime = 0.0;
    std::string m_titlePath = "\x01";  // path the window title was last set for

    void drawDockSpace();
    void drawMenuBar(const OverlayContext& context);
    void drawPlayButtons(const OverlayContext& context);
    void setPlaying(const OverlayContext& context, bool playing);
    void undo(const OverlayContext& context);
    void redo(const OverlayContext& context);
    std::uint64_t selectionUuid(const Scene& scene) const;
    void setSelection(const Scene& scene, std::uint64_t uuid);
    void recordSelection(const Scene& scene);

    void newScene(const OverlayContext& context);
    void openScene(const OverlayContext& context, const std::string& path);
    void saveScene(const OverlayContext& context, const std::string& path);
    void showOpenDialog();
    void showSaveDialog();
    void applyDialogResult(const OverlayContext& context);
    void setStatus(std::string message);
    void updateTitle(const std::string& path);
    static void onDialog(void* editor, const char* const* files, bool save);

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

    void update(const OverlayContext& context) override;
    void renderViews(const OverlayContext& context) override;
    void render() override;
};
