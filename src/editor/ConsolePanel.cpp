#include "ConsolePanel.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace
{
constexpr std::size_t MaxLines = 10000;
const ImVec4 ErrorColor(1.0f, 0.45f, 0.4f, 1.0f);
}  // namespace

bool ConsolePanel::passes(const logcapture::Line& line) const
{
    if (!(line.error ? m_showErrors : m_showOutput))
        return false;
    if (!m_filter[0])
        return true;
    const std::string_view needle(m_filter);
    const auto lower = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    return std::search(line.text.begin(), line.text.end(), needle.begin(), needle.end(), lower) != line.text.end();
}

void ConsolePanel::draw(unsigned int fallbackDock)
{
    logcapture::drain(m_lines);
    if (m_lines.size() > MaxLines)
        m_lines.erase(m_lines.begin(), m_lines.begin() + static_cast<std::ptrdiff_t>(m_lines.size() - MaxLines));

    if (!m_open)
        return;

    // Layouts saved before the console existed don't place it
    if (fallbackDock && !ImGui::FindWindowSettingsByID(ImHashStr(WindowName)))
        ImGui::SetNextWindowDockID(fallbackDock, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(WindowName, &m_open)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear"))
        m_lines.clear();
    ImGui::SameLine();
    if (ImGui::Button("Copy")) {
        std::string text;
        for (const logcapture::Line& line : m_lines) {
            if (passes(line)) {
                text += line.text;
                text += '\n';
            }
        }
        ImGui::SetClipboardText(text.c_str());
    }
    ImGui::SameLine();
    ImGui::Checkbox("stdout", &m_showOutput);
    ImGui::SameLine();
    ImGui::Checkbox("stderr", &m_showErrors);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter", m_filter, sizeof(m_filter));
    ImGui::Separator();

    if (ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
        const bool filtering = m_filter[0] || !m_showOutput || !m_showErrors;
        auto drawLine = [](const logcapture::Line& line) {
            if (line.error)
                ImGui::PushStyleColor(ImGuiCol_Text, ErrorColor);
            ImGui::TextUnformatted(line.text.data(), line.text.data() + line.text.size());
            if (line.error)
                ImGui::PopStyleColor();
        };
        if (filtering) {
            for (const logcapture::Line& line : m_lines)
                if (passes(line))
                    drawLine(line);
        } else {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(m_lines.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    drawLine(m_lines[static_cast<std::size_t>(i)]);
        }
        ImGui::PopStyleVar();
        // Follows new lines only while already at the bottom, so scrolling up to read stays put
        if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}
