#pragma once

#include "engine/assets/AssetTypes.h"

#include <entt/entity/entity.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class ThumbnailCache;

// Unity-style Project window over the assets folder, which it never leaves: folder tree on
// the left, the open folder's contents as icons and thumbnails on the right. Double-click
// opens folders and scenes (other files open in their default app). Drag items onto folders
// to move them, into the Scene view or Hierarchy to use them; drag Hierarchy objects in to
// make prefabs. F2 renames, Del deletes, Ctrl+D duplicates, Backspace goes up a folder.
class ProjectPanel
{
public:
    // What the editor has to act on, collected during draw()
    struct Events
    {
        std::optional<std::string> openScene;    // ref
        std::optional<std::string> openPrefab;   // ref
        std::optional<std::string> instantiate;  // ref of a model or prefab
        std::optional<std::string> openAnimationViewer;  // ref of a model; the editor checks it's skinned
        std::vector<std::pair<std::string, std::string>> moved;  // refs: from, to
        std::vector<std::string> deleted;        // refs
        std::optional<std::filesystem::path> createScene;  // in this folder
        std::optional<std::pair<entt::entity, std::filesystem::path>> createPrefab;  // object, folder
        std::vector<std::string> errors;
    };

private:
    using path = std::filesystem::path;

    struct Item
    {
        path file;
        std::string ref;
        std::string label;  // shown under the icon: files without their extension
        AssetKind kind;
        std::filesystem::file_time_type stamp;
    };

    path m_current;
    std::string m_selected;  // ref
    std::vector<Item> m_items;
    std::unordered_map<std::string, std::vector<path>> m_subfolders;  // by folder, for the tree
    bool m_rescan = true;
    double m_lastScan = -1.0;
    char m_search[128] = {};
    std::string m_scannedSearch;
    float m_iconSize = 72.0f;
    bool m_focused = false;
    bool m_revealCurrent = true;  // open the tree down to m_current
    std::string m_scrollTo;
    std::string m_pinged;  // flashes for a moment
    double m_pingTime = 0.0;

    std::string m_renaming;  // ref
    char m_renameBuffer[256] = {};
    bool m_renameFocus = false;
    std::string m_pendingDelete;  // ref waiting for confirmation
    bool m_openDeletePopup = false;

    Events m_events;

    void scan();
    const std::vector<path>& subfolders(const path& dir);
    void navigate(const path& dir);
    void drawToolbar();
    void drawTree(const path& dir, bool isRoot);
    void drawGrid(ThumbnailCache& thumbnails);
    void drawItem(const Item& item, ThumbnailCache& thumbnails);
    void drawItemMenu(const Item& item);
    void drawCreateMenu();
    void drawDeletePopup();
    void handleShortcuts();
    void acceptDrops(const path& folder);
    void activate(const Item& item);
    void commitRename();
    void moveInto(const std::string& ref, const path& folder);
    void duplicateSelected();
    void fail(std::string message) { m_events.errors.push_back(std::move(message)); }
    const Item* findItem(const std::string& ref) const;

public:
    ProjectPanel();

    void draw(ThumbnailCache& thumbnails);
    Events takeEvents() { return std::exchange(m_events, {}); }

    const std::string& selected() const { return m_selected; }
    // Also opens its folder and scrolls to it; "" clears the selection
    void select(const std::string& ref);
    void clearSelection() { m_selected.clear(); }
    // Opens its folder and flashes it, without selecting it
    void ping(const std::string& ref);
    void beginRename(const std::string& ref);
    void refresh() { m_rescan = true; }
    const std::filesystem::path& currentFolder() const { return m_current; }
    bool focused() const { return m_focused; }

    // In the open folder, then renamed in place
    void createFolder();
    void createMaterial();
    void createScene() { m_events.createScene = m_current; }
    void showInFileManager(const std::filesystem::path& file) const;

    static constexpr const char* WindowName = "Project";
};

// Opens path with the desktop's default application
void openExternally(const std::filesystem::path& path);
