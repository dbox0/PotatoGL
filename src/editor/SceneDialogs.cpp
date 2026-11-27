#include "SceneDialogs.h"

#include "editor/AssetOps.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

namespace fs = std::filesystem;

static const char* SaveAsTitle = "Save Scene As";
static const char* OpenTitle = "Open Scene";
static const char* UnsavedTitle = "Unsaved Changes";

void SceneDialogs::openSaveAs(const std::string& currentPath)
{
    m_folders = assetops::findAssets(AssetKind::Folder);
    if (std::find(m_folders.begin(), m_folders.end(), DefaultFolder) == m_folders.end()) {
        m_folders.push_back(DefaultFolder);  // created on save
        std::sort(m_folders.begin(), m_folders.end());
    }
    const fs::path current(currentPath);
    if (!currentPath.empty() && assetops::isInsideRoot(current)) {
        m_folder = assetops::toRef(current.parent_path());
        std::snprintf(m_name, sizeof(m_name), "%s", current.stem().string().c_str());
    } else {
        m_folder = DefaultFolder;
        const fs::path free = assetops::uniquePath(assetops::fromRef(m_folder), "New Scene", ".scene");
        std::snprintf(m_name, sizeof(m_name), "%s", free.stem().string().c_str());
    }
    m_focusName = true;
    m_showSaveAs = true;
}

void SceneDialogs::openOpenScene()
{
    m_scenes = assetops::findAssets(AssetKind::Scene);
    m_filter[0] = '\0';
    m_highlighted.clear();
    m_showOpen = true;
}

void SceneDialogs::openUnsaved(const std::string& sceneName)
{
    m_unsavedScene = sceneName;
    m_showUnsaved = true;
}

void SceneDialogs::draw()
{
    if (std::exchange(m_showSaveAs, false))
        ImGui::OpenPopup(SaveAsTitle);
    if (std::exchange(m_showOpen, false))
        ImGui::OpenPopup(OpenTitle);
    if (std::exchange(m_showUnsaved, false))
        ImGui::OpenPopup(UnsavedTitle);
    drawSaveAs();
    drawOpen();
    drawUnsaved();
}

void SceneDialogs::drawSaveAs()
{
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(SaveAsTitle, nullptr, ImGuiWindowFlags_NoSavedSettings))
        return;

    ImGui::PushItemWidth(-70.0f);
    if (std::exchange(m_focusName, false))
        ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("Name", m_name, sizeof(m_name), ImGuiInputTextFlags_EnterReturnsTrue |
                                                                          ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::BeginCombo("Folder", assetops::displayPath(m_folder).c_str())) {
        for (const std::string& folder : m_folders) {
            if (ImGui::Selectable(assetops::displayPath(folder).c_str(), folder == m_folder))
                m_folder = folder;
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    std::string name = m_name;
    if (name.size() > 6 && name.ends_with(".scene"))
        name.resize(name.size() - 6);
    const bool valid = assetops::validName(name);
    const fs::path target = assetops::fromRef(m_folder) / (name + ".scene");
    std::error_code ec;
    const bool exists = valid && fs::exists(target, ec);

    ImGui::Spacing();
    if (!valid)
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Enter a valid file name");
    else if (exists)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s exists and will be replaced",
                           assetops::displayPath(assetops::toRef(target)).c_str());
    else
        ImGui::TextDisabled("%s", assetops::displayPath(assetops::toRef(target)).c_str());
    ImGui::Spacing();

    ImGui::BeginDisabled(!valid);
    if (ImGui::Button(exists ? "Replace" : "Save", ImVec2(100.0f, 0.0f)) || (enter && valid)) {
        m_savePath = target;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_saveAsCancelled = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void SceneDialogs::drawOpen()
{
    ImGui::SetNextWindowSize(ImVec2(440.0f, 360.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(OpenTitle, nullptr, ImGuiWindowFlags_NoSavedSettings))
        return;

    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Search scenes", m_filter, sizeof(m_filter));
    std::string query = m_filter;
    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return std::tolower(c); });

    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    std::string firstMatch;
    if (ImGui::BeginChild("##scenes", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders)) {
        for (const std::string& scene : m_scenes) {
            std::string label = assetops::displayPath(scene);
            std::string lowered = label;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return std::tolower(c); });
            if (!query.empty() && lowered.find(query) == std::string::npos)
                continue;
            if (firstMatch.empty())
                firstMatch = scene;
            if (ImGui::Selectable(label.c_str(), scene == m_highlighted, ImGuiSelectableFlags_AllowDoubleClick)) {
                m_highlighted = scene;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    m_openRef = scene;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        if (m_scenes.empty())
            ImGui::TextDisabled("No scenes in the project yet");
    }
    ImGui::EndChild();

    const std::string chosen = m_highlighted.empty() ? firstMatch : m_highlighted;
    ImGui::BeginDisabled(chosen.empty());
    if (ImGui::Button("Open", ImVec2(100.0f, 0.0f)) ||
        (!chosen.empty() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)))) {
        m_openRef = chosen;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void SceneDialogs::drawUnsaved()
{
    if (!ImGui::BeginPopupModal(UnsavedTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        return;
    ImGui::Text("Do you want to save the changes you made in %s?", m_unsavedScene.c_str());
    ImGui::TextDisabled("Your changes will be lost if you don't save them.");
    ImGui::Spacing();
    if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
        m_choice = Choice::Save;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Don't Save", ImVec2(100.0f, 0.0f))) {
        m_choice = Choice::Discard;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_choice = Choice::Cancel;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
