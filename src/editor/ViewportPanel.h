#pragma once

#include "engine/gfx/RenderTarget.h"

#include <functional>

// Dockable window showing a RenderTarget. draw() sizes the target to the window;
// the scene is rendered into it later in the same frame.
class ViewportPanel
{
private:
    const char* m_name;
    RenderTarget m_target;
    bool m_visible = false;
    bool m_hovered = false;
    bool m_focused = false;
    bool m_focusRequested = false;
    float m_imageX = 0.0f, m_imageY = 0.0f;  // screen position and size of the image
    float m_imageW = 0.0f, m_imageH = 0.0f;

public:
    explicit ViewportPanel(const char* name) : m_name(name) {}

    // placeholder: shown instead of the image when non-null. overlay runs inside the window
    // right after the image, so ImGui::IsItem*() refer to it.
    void draw(const char* placeholder = nullptr, const std::function<void()>& overlay = {});
    void release() { m_target.release(); }
    void focus() { m_focusRequested = true; }  // applied on the next draw

    const char* name() const           { return m_name; }
    const RenderTarget& target() const { return m_target; }
    float aspect() const;
    float imageX() const               { return m_imageX; }
    float imageY() const               { return m_imageY; }
    float imageWidth() const           { return m_imageW; }
    float imageHeight() const          { return m_imageH; }
    bool visible() const               { return m_visible && m_target.valid(); }
    bool hovered() const               { return m_hovered; }
    bool focused() const               { return m_focused; }
};
