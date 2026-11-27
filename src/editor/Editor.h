#pragma once

#include "engine/app/Overlay.h"
#include "editor/DebugPanel.h"
#include "editor/InspectorPanel.h"
#include "editor/SceneHierarchyPanel.h"
#include "editor/SceneViewPanel.h"
#include "editor/ViewportPanel.h"

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

    void drawDockSpace();
    void drawToolbar(PlayMode& mode);

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
