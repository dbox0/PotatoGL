#include "ProfilerPanel.h"

#include "engine/gfx/GpuProfiler.h"

#include <imgui.h>

#include <cstdio>

void ProfilerPanel::draw(GpuProfiler& profiler)
{
    profiler.setEnabled(m_open);
    if (!m_open)
        return;

    ImGui::SetNextWindowSize(ImVec2(460.0f, 260.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Profiler", &m_open)) {
        const auto& results = profiler.results();
        if (results.empty()) {
            ImGui::TextDisabled("Collecting...");
        } else {
            const double frameGpu = results.front().gpuMs;
            constexpr ImGuiTableFlags flags =
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("passes", 4, flags)) {
                ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 1.6f);
                ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthStretch, 0.7f);
                ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthStretch, 0.7f);
                ImGui::TableSetupColumn("GPU share", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableHeadersRow();
                for (const GpuProfiler::Result& r : results) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Indent(static_cast<float>(r.depth) * 12.0f);
                    ImGui::TextUnformatted(r.name.data(), r.name.data() + r.name.size());
                    ImGui::Unindent(static_cast<float>(r.depth) * 12.0f);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", r.gpuMs);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", r.cpuMs);
                    ImGui::TableNextColumn();
                    const float share = frameGpu > 0.0 ? static_cast<float>(r.gpuMs / frameGpu) : 0.0f;
                    char label[16];
                    std::snprintf(label, sizeof(label), "%.0f%%", share * 100.0f);
                    ImGui::ProgressBar(share, ImVec2(-1.0f, 0.0f), label);
                }
                ImGui::EndTable();
            }
        }
        ImGui::TextDisabled("Smoothed, %d frames behind. Frame excludes the swap.", 4);
    }
    ImGui::End();
}
