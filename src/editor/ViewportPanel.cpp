#include "ViewportPanel.h"

#include <imgui.h>


void ViewportPanel::draw(const char* placeholder, const std::function<void()>& overlay)
{
    if (m_focusRequested) {
        ImGui::SetNextWindowFocus();
        m_focusRequested = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    m_visible = ImGui::Begin(m_name, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    m_hovered = m_visible && ImGui::IsWindowHovered();
    m_focused = m_visible && ImGui::IsWindowFocused();
    if (m_visible) {
        const ImVec2 size = ImGui::GetContentRegionAvail();
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        m_target.resize(static_cast<int>(size.x * scale.x), static_cast<int>(size.y * scale.y));

        if (placeholder) {
            m_visible = false;
            ImGui::SetCursorPos(ImVec2(10.0f, 10.0f));
            ImGui::TextDisabled("%s", placeholder);
        } else if (m_target.valid()) {
            const ImVec2 screen = ImGui::GetCursorScreenPos();
            m_imageX = screen.x;
            m_imageY = screen.y;
            m_imageW = size.x;
            m_imageH = size.y;
            // GL textures are bottom-up
            const auto texture = static_cast<ImTextureID>(m_target.displayTexture());
            ImGui::Image(texture, size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            if (overlay)
                overlay();
        }
    }
    ImGui::End();
}

float ViewportPanel::aspect() const
{
    return m_target.height() > 0 ? static_cast<float>(m_target.width()) / static_cast<float>(m_target.height())
                                 : 1.0f;
}
