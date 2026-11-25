#pragma once

#include "engine/app/Overlay.h"
#include "editor/DebugPanel.h"
#include "editor/SceneHierarchyPanel.h"

// Dear ImGui editor overlay; F1 toggles it. The only place that touches ImGui.
class Editor final : public Overlay
{
private:
    bool m_initialized = false;
    bool m_visible = true;
    DebugPanel m_debugPanel;
    SceneHierarchyPanel m_hierarchyPanel;

public:
    Editor() = default;
    ~Editor() override;

    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    bool init(SDL_Window* window, SDL_GLContext context) override;
    void shutdown() override;

    void processEvent(const SDL_Event& event) override;
    bool wantsMouse() const override;
    bool wantsKeyboard() const override;

    void update(const OverlayContext& context) override;
    void render() override;
};
