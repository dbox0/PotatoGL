#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The editor's scene file dialogs, all inside the Assets folder: Save As (new scenes go to
// Assets/Scenes by default), Open Scene (every scene in the project) and the unsaved
// changes prompt. open*() shows one; draw() runs them; take*() hands out the answer once.
class SceneDialogs
{
public:
    enum class Choice
    {
        None,
        Save,
        Discard,
        Cancel,
    };

private:
    bool m_showSaveAs = false;
    bool m_showOpen = false;
    bool m_showUnsaved = false;

    char m_name[128] = {};
    std::string m_folder;  // ref
    std::vector<std::string> m_folders;
    bool m_focusName = false;

    char m_filter[128] = {};
    std::vector<std::string> m_scenes;
    std::string m_highlighted;

    std::string m_unsavedScene;

    std::optional<std::filesystem::path> m_savePath;
    std::optional<std::string> m_openRef;
    Choice m_choice = Choice::None;
    bool m_saveAsCancelled = false;

    void drawSaveAs();
    void drawOpen();
    void drawUnsaved();

public:
    static constexpr const char* DefaultFolder = "assets/Scenes";

    // currentPath: the scene's file, empty if it was never saved
    void openSaveAs(const std::string& currentPath);
    void openOpenScene();
    void openUnsaved(const std::string& sceneName);

    void draw();

    std::optional<std::filesystem::path> takeSavePath() { return std::exchange(m_savePath, std::nullopt); }
    std::optional<std::string> takeOpenRef()            { return std::exchange(m_openRef, std::nullopt); }
    Choice takeUnsavedChoice()                          { return std::exchange(m_choice, Choice::None); }
    bool takeSaveAsCancelled()                          { return std::exchange(m_saveAsCancelled, false); }
};
