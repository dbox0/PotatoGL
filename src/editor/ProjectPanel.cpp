#include "ProjectPanel.h"

#include "editor/AssetOps.h"
#include "editor/DragDrop.h"
#include "editor/ThumbnailCache.h"

#include "engine/assets/AssetCache.h"

#include <SDL3/SDL_misc.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace fs = std::filesystem;

static std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

static std::string fileUrl(const fs::path& path)
{
    std::string url = "file://";
    for (const unsigned char c : path.generic_string()) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
            url += static_cast<char>(c);
        } else {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%%%02X", c);
            url += hex;
        }
    }
    return url;
}

void openExternally(const fs::path& path)
{
    if (!SDL_OpenURL(fileUrl(path).c_str()))
        std::fprintf(stderr, "Can't open %s: %s\n", path.string().c_str(), SDL_GetError());
}

ProjectPanel::ProjectPanel() : m_current(assetops::root()) {}

void ProjectPanel::showInFileManager(const fs::path& file) const
{
    std::error_code ec;
    openExternally(fs::is_directory(file, ec) ? file : file.parent_path());
}

static ImU32 kindColor(AssetKind kind)
{
    switch (kind) {
    case AssetKind::Scene:    return IM_COL32(96, 170, 110, 255);
    case AssetKind::Prefab:   return IM_COL32(80, 150, 230, 255);
    case AssetKind::Material: return IM_COL32(190, 110, 210, 255);
    case AssetKind::Texture:  return IM_COL32(210, 170, 80, 255);
    case AssetKind::Model:    return IM_COL32(90, 190, 200, 255);
    case AssetKind::Script:   return IM_COL32(110, 140, 230, 255);
    case AssetKind::Shader:   return IM_COL32(220, 120, 90, 255);
    case AssetKind::Animation: return IM_COL32(230, 150, 60, 255);
    case AssetKind::Pose:     return IM_COL32(220, 190, 90, 255);
    default:                  return IM_COL32(130, 130, 135, 255);
    }
}

static std::string kindTag(AssetKind kind, const fs::path& file)
{
    switch (kind) {
    case AssetKind::Scene:    return "SCENE";
    case AssetKind::Prefab:   return "PREFAB";
    case AssetKind::Material: return "MAT";
    case AssetKind::Script:   return "C++";
    case AssetKind::Shader:   return "GLSL";
    default: {
        std::string ext = file.extension().string();
        if (!ext.empty())
            ext.erase(0, 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::toupper(c); });
        return ext.empty() ? "FILE" : ext.substr(0, 6);
    }
    }
}

static void drawFolderIcon(ImDrawList* dl, ImVec2 min, ImVec2 max)
{
    const float w = max.x - min.x, h = max.y - min.y;
    const float rounding = w * 0.05f;
    dl->AddRectFilled(ImVec2(min.x + w * 0.08f, min.y + h * 0.16f), ImVec2(min.x + w * 0.46f, min.y + h * 0.34f),
                      IM_COL32(176, 136, 64, 255), rounding);
    dl->AddRectFilled(ImVec2(min.x + w * 0.08f, min.y + h * 0.26f), ImVec2(max.x - w * 0.08f, max.y - h * 0.14f),
                      IM_COL32(222, 176, 88, 255), rounding);
}

static void drawFileIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, AssetKind kind, const std::string& tag)
{
    const float w = max.x - min.x, h = max.y - min.y;
    const ImVec2 a(min.x + w * 0.16f, min.y + h * 0.06f), b(max.x - w * 0.16f, max.y - h * 0.06f);
    const float rounding = w * 0.05f;
    dl->AddRectFilled(a, b, IM_COL32(58, 60, 66, 255), rounding);
    const float band = (b.y - a.y) * 0.3f;
    dl->AddRectFilled(ImVec2(a.x, b.y - band), b, kindColor(kind), rounding, ImDrawFlags_RoundCornersBottom);
    const ImVec2 size = ImGui::CalcTextSize(tag.c_str());
    dl->AddText(ImVec2((a.x + b.x - size.x) * 0.5f, b.y - band * 0.5f - size.y * 0.5f), IM_COL32(20, 20, 24, 255),
                tag.c_str());
}

// Thumbnail fitted to [min, max] keeping its aspect, or a drawn icon
static void drawIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, const fs::path& file, const std::string& ref,
                     AssetKind kind, fs::file_time_type stamp, ThumbnailCache& thumbnails)
{
    if (kind == AssetKind::Folder) {
        drawFolderIcon(dl, min, max);
        return;
    }
    if (const ThumbnailCache::Image image = thumbnails.get(ref, kind, stamp)) {
        const float w = max.x - min.x, h = max.y - min.y;
        const float scale = std::min(w / static_cast<float>(image.width), h / static_cast<float>(image.height));
        const ImVec2 size(static_cast<float>(image.width) * scale, static_cast<float>(image.height) * scale);
        const ImVec2 a(min.x + (w - size.x) * 0.5f, min.y + (h - size.y) * 0.5f);
        const ImVec2 b(a.x + size.x, a.y + size.y);
        const auto texture = static_cast<ImTextureID>(image.texture);
        if (image.flipY)
            dl->AddImageRounded(texture, a, b, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), IM_COL32_WHITE, w * 0.04f);
        else
            dl->AddImage(texture, a, b);
        return;
    }
    drawFileIcon(dl, min, max, kind, kindTag(kind, file));
}

// Fits text into width, ending in "..." if it has to be cut
static std::string ellipsize(const std::string& text, float width)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= width)
        return text;
    std::string out = text;
    while (!out.empty() && ImGui::CalcTextSize((out + "...").c_str()).x > width)
        out.pop_back();
    return out + "...";
}

const std::vector<fs::path>& ProjectPanel::subfolders(const fs::path& dir)
{
    const auto [it, inserted] = m_subfolders.try_emplace(dir.string());
    if (inserted) {
        std::error_code ec;
        for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
            if (entry.is_directory(ec) && !entry.path().filename().string().starts_with('.'))
                it->second.push_back(entry.path());
        }
        std::sort(it->second.begin(), it->second.end(), [](const fs::path& a, const fs::path& b) {
            return lower(a.filename().string()) < lower(b.filename().string());
        });
    }
    return it->second;
}

void ProjectPanel::scan()
{
    std::error_code ec;
    if (!fs::is_directory(m_current, ec) || !assetops::isInsideRoot(m_current))
        m_current = assetops::root();

    m_items.clear();
    m_subfolders.clear();
    m_scannedSearch = m_search;
    const std::string query = lower(m_scannedSearch);

    const auto add = [&](const fs::directory_entry& entry) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with('.') || name.ends_with(".meta"))  // sidecars move with their asset
            return;
        const bool folder = entry.is_directory(ec);
        if (!query.empty() && lower(name).find(query) == std::string::npos)
            return;
        Item item;
        item.file = entry.path();
        item.ref = assetops::toRef(entry.path());
        item.kind = folder ? AssetKind::Folder : assetKindOf(name);
        item.label = folder || entry.path().stem().empty() ? name : entry.path().stem().string();
        item.stamp = entry.last_write_time(ec);
        m_items.push_back(std::move(item));
    };

    if (query.empty()) {
        for (const fs::directory_entry& entry : fs::directory_iterator(m_current, ec))
            add(entry);
    } else {
        for (auto it = fs::recursive_directory_iterator(assetops::root(), fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->path().filename().string().starts_with('.')) {
                it.disable_recursion_pending();
                continue;
            }
            add(*it);
        }
    }

    std::sort(m_items.begin(), m_items.end(), [](const Item& a, const Item& b) {
        const bool af = a.kind == AssetKind::Folder, bf = b.kind == AssetKind::Folder;
        if (af != bf)
            return af;
        const std::string la = lower(a.label), lb = lower(b.label);
        return la != lb ? la < lb : a.file.filename() < b.file.filename();
    });
    m_rescan = false;
    m_lastScan = ImGui::GetTime();
}

void ProjectPanel::navigate(const fs::path& dir)
{
    if (!assetops::isInsideRoot(dir))
        return;
    m_current = dir.lexically_normal();
    m_search[0] = '\0';
    m_revealCurrent = true;
    m_rescan = true;
}

void ProjectPanel::select(const std::string& ref)
{
    m_selected = ref;
    if (ref.empty())
        return;
    const fs::path file = assetops::fromRef(ref);
    if (assetops::isInsideRoot(file) && file != assetops::root() && file.parent_path() != m_current)
        navigate(file.parent_path());
    m_scrollTo = ref;
}

void ProjectPanel::ping(const std::string& ref)
{
    const fs::path file = assetops::fromRef(ref);
    if (!assetops::isInsideRoot(file) || file == assetops::root())
        return;
    if (file.parent_path() != m_current)
        navigate(file.parent_path());
    m_scrollTo = ref;
    m_pinged = ref;
    m_pingTime = ImGui::GetTime();
}

void ProjectPanel::beginRename(const std::string& ref)
{
    const fs::path file = assetops::fromRef(ref);
    if (file == assetops::root())
        return;
    select(ref);
    m_renaming = ref;
    std::error_code ec;
    const std::string name = fs::is_directory(file, ec) || file.stem().empty() ? file.filename().string()
                                                                               : file.stem().string();
    std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", name.c_str());
    m_renameFocus = true;
}

void ProjectPanel::commitRename()
{
    const std::string ref = std::exchange(m_renaming, {});
    const fs::path from = assetops::fromRef(ref);
    std::error_code ec;
    const bool folder = fs::is_directory(from, ec);
    std::string name = m_renameBuffer;
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    while (!name.empty() && name.front() == ' ')
        name.erase(0, 1);
    if (!folder && !from.stem().empty())
        name += from.extension().string();
    if (name == from.filename().string())
        return;
    if (!assetops::validName(name)) {
        fail("'" + name + "' isn't a valid name");
        return;
    }
    const fs::path to = from.parent_path() / name;
    std::string error;
    if (!assetops::move(from, to, &error)) {
        fail("Rename failed: " + error);
        return;
    }
    const std::string toRef = assetops::toRef(to);
    m_events.moved.emplace_back(ref, toRef);
    if (pathIsUnder(m_current.generic_string(), from.generic_string()))
        m_current = to / m_current.lexically_relative(from);
    m_selected = toRef;
    m_scrollTo = toRef;
    m_rescan = true;
}

void ProjectPanel::moveInto(const std::string& ref, const fs::path& folder)
{
    const fs::path from = assetops::fromRef(ref);
    if (from.parent_path() == folder.lexically_normal() || from == folder.lexically_normal())
        return;
    const fs::path to = folder / from.filename();
    std::string error;
    if (!assetops::move(from, to, &error)) {
        fail("Move failed: " + error);
        return;
    }
    const std::string toRef = assetops::toRef(to);
    m_events.moved.emplace_back(ref, toRef);
    if (pathIsUnder(m_selected, ref))
        m_selected = rebasePath(m_selected, ref, toRef);
    if (pathIsUnder(m_current.generic_string(), from.generic_string()))
        m_current = to / m_current.lexically_relative(from);
    m_rescan = true;
}

void ProjectPanel::duplicateSelected()
{
    if (m_selected.empty())
        return;
    std::string error;
    if (const auto copy = assetops::duplicate(assetops::fromRef(m_selected), &error)) {
        select(assetops::toRef(*copy));
        m_rescan = true;
    } else {
        fail("Duplicate failed: " + error);
    }
}

void ProjectPanel::createFolder()
{
    std::string error;
    if (const auto folder = assetops::createFolder(m_current, &error)) {
        m_rescan = true;
        beginRename(assetops::toRef(*folder));
    } else {
        fail(error);
    }
}

void ProjectPanel::createMaterial()
{
    std::string error;
    if (const auto material = assetops::createMaterial(m_current, &error)) {
        m_rescan = true;
        beginRename(assetops::toRef(*material));
    } else {
        fail("Can't create material: " + error);
    }
}

const ProjectPanel::Item* ProjectPanel::findItem(const std::string& ref) const
{
    for (const Item& item : m_items) {
        if (item.ref == ref)
            return &item;
    }
    return nullptr;
}

void ProjectPanel::activate(const Item& item)
{
    switch (item.kind) {
    case AssetKind::Folder:
        navigate(item.file);
        break;
    case AssetKind::Scene:
        m_events.openScene = item.ref;
        break;
    case AssetKind::Prefab:
        m_events.openPrefab = item.ref;
        break;
    case AssetKind::Model:
    case AssetKind::Animation:
    case AssetKind::Pose:
        m_events.openAnimationViewer = item.ref;
        break;
    case AssetKind::Material:
        break;  // the inspector shows it
    default:
        openExternally(item.file);
        break;
    }
}

void ProjectPanel::acceptDrops(const fs::path& folder)
{
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetPayload))
        moveInto(static_cast<const char*>(payload->Data), folder);
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EntityPayload))
        m_events.createPrefab = std::make_pair(*static_cast<const entt::entity*>(payload->Data), folder);
}

void ProjectPanel::draw(ThumbnailCache& thumbnails)
{
    m_focused = false;
    if (!ImGui::Begin(WindowName)) {
        ImGui::End();
        return;
    }
    m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (m_rescan || m_scannedSearch != m_search || ImGui::GetTime() - m_lastScan > 1.0)
        scan();

    drawToolbar();
    ImGui::Separator();

    if (ImGui::BeginTable("##layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Contents", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("##tree")) {
            drawTree(assetops::root(), true);
            m_revealCurrent = false;
        }
        ImGui::EndChild();
        ImGui::TableSetColumnIndex(1);
        if (ImGui::BeginChild("##grid"))
            drawGrid(thumbnails);
        ImGui::EndChild();
        ImGui::EndTable();
    }

    handleShortcuts();
    if (m_openDeletePopup) {
        ImGui::OpenPopup("Delete Asset?");
        m_openDeletePopup = false;
    }
    drawDeletePopup();
    ImGui::End();
}

void ProjectPanel::drawToolbar()
{
    // Breadcrumbs; each one is also a drop target
    const fs::path root = assetops::root();
    std::vector<fs::path> crumbs;
    for (fs::path p = m_current; ; p = p.parent_path()) {
        crumbs.push_back(p);
        if (p == root || !assetops::isInsideRoot(p.parent_path()))
            break;
    }
    std::reverse(crumbs.begin(), crumbs.end());

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    for (std::size_t i = 0; i < crumbs.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::TextDisabled(">");
            ImGui::SameLine(0.0f, 2.0f);
        }
        ImGui::PushID(static_cast<int>(i));
        const std::string label = i == 0 ? std::string("Assets") : crumbs[i].filename().string();
        if (ImGui::SmallButton(label.c_str()))
            navigate(crumbs[i]);
        if (ImGui::BeginDragDropTarget()) {
            acceptDrops(crumbs[i]);
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
    }
    ImGui::PopStyleColor();

    const float sliderWidth = 90.0f;
    const float searchWidth = 180.0f;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float right = ImGui::GetContentRegionMax().x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + style.ItemSpacing.x,
                             right - searchWidth - sliderWidth - style.ItemSpacing.x));
    ImGui::SetNextItemWidth(searchWidth);
    ImGui::InputTextWithHint("##search", "Search", m_search, sizeof(m_search));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(sliderWidth);
    ImGui::SliderFloat("##size", &m_iconSize, 40.0f, 160.0f, "");
    ImGui::SetItemTooltip("Icon size");
}

void ProjectPanel::drawTree(const fs::path& dir, bool isRoot)
{
    const std::vector<fs::path>& children = subfolders(dir);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (dir == m_current && m_scannedSearch.empty())
        flags |= ImGuiTreeNodeFlags_Selected;
    if (isRoot)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (m_revealCurrent && dir != m_current && pathIsUnder(m_current.generic_string(), dir.generic_string()))
        ImGui::SetNextItemOpen(true);

    const std::string name = isRoot ? std::string("Assets") : dir.filename().string();
    const bool open = ImGui::TreeNodeEx(dir.string().c_str(), flags, "%s", name.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        navigate(dir);
    if (!isRoot && ImGui::BeginDragDropSource()) {
        setAssetPayload(assetops::toRef(dir));
        ImGui::TextUnformatted(name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        acceptDrops(dir);
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem()) {
        if (m_current != dir)
            navigate(dir);
        drawCreateMenu();
        ImGui::Separator();
        if (ImGui::MenuItem("Show in File Manager"))
            showInFileManager(dir);
        if (!isRoot) {
            if (ImGui::MenuItem("Rename"))
                beginRename(assetops::toRef(dir));
            if (ImGui::MenuItem("Delete")) {
                m_pendingDelete = assetops::toRef(dir);
                m_openDeletePopup = true;
            }
        }
        ImGui::EndPopup();
    }

    if (open) {
        for (const fs::path& child : children)
            drawTree(child, false);
        ImGui::TreePop();
    }
}

void ProjectPanel::drawCreateMenu()
{
    if (ImGui::BeginMenu("Create")) {
        if (ImGui::MenuItem("Folder"))
            createFolder();
        ImGui::Separator();
        if (ImGui::MenuItem("Scene"))
            createScene();
        if (ImGui::MenuItem("Material"))
            createMaterial();
        ImGui::EndMenu();
    }
}

void ProjectPanel::drawGrid(ThumbnailCache& thumbnails)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float cellWidth = m_iconSize + 8.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(1, static_cast<int>((avail + style.ItemSpacing.x) / (cellWidth + style.ItemSpacing.x)));

    if (m_items.empty())
        ImGui::TextDisabled(m_scannedSearch.empty() ? "This folder is empty" : "No results");
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        drawItem(m_items[i], thumbnails);
        if ((i + 1) % static_cast<std::size_t>(columns) != 0 && i + 1 < m_items.size())
            ImGui::SameLine();
    }

    // Empty space: clear the selection, the create menu, drop to move here or make a prefab
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        m_selected.clear();
    if (ImGui::BeginPopupContextWindow("##background", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        drawCreateMenu();
        ImGui::Separator();
        if (ImGui::MenuItem("Show in File Manager"))
            showInFileManager(m_current);
        if (ImGui::MenuItem("Refresh"))
            m_rescan = true;
        ImGui::EndPopup();
    }
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (ImGui::BeginDragDropTargetCustom(window->InnerRect, window->ID)) {
        acceptDrops(m_current);
        ImGui::EndDragDropTarget();
    }
}

void ProjectPanel::drawItem(const Item& item, ThumbnailCache& thumbnails)
{
    ImGui::PushID(item.ref.c_str());
    const float lineHeight = ImGui::GetTextLineHeight();
    const ImVec2 cell(m_iconSize + 8.0f, m_iconSize + lineHeight + 12.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool renaming = item.ref == m_renaming;

    ImGui::BeginGroup();
    if (ImGui::Selectable("##item", item.ref == m_selected,
                          ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap, cell))
        m_selected = item.ref;
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !renaming)
        activate(item);
    if (m_scrollTo == item.ref) {
        ImGui::SetScrollHereY();
        m_scrollTo.clear();
    }
    if (ImGui::BeginDragDropSource()) {
        setAssetPayload(item.ref);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        drawIcon(ImGui::GetWindowDrawList(), at, ImVec2(at.x + 40.0f, at.y + 40.0f), item.file, item.ref, item.kind,
                 item.stamp, thumbnails);
        ImGui::Dummy(ImVec2(40.0f, 40.0f));
        ImGui::SameLine();
        ImGui::TextUnformatted(item.label.c_str());
        ImGui::EndDragDropSource();
    }
    if (item.kind == AssetKind::Folder && ImGui::BeginDragDropTarget()) {
        acceptDrops(item.file);
        ImGui::EndDragDropTarget();
    }
    if (!renaming && !ImGui::IsDragDropActive())
        ImGui::SetItemTooltip("%s", item.file.filename().string().c_str());
    if (ImGui::BeginPopupContextItem("##menu")) {
        m_selected = item.ref;
        drawItemMenu(item);
        ImGui::EndPopup();
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 iconMin(pos.x + 4.0f, pos.y + 4.0f);
    drawIcon(dl, iconMin, ImVec2(iconMin.x + m_iconSize, iconMin.y + m_iconSize), item.file, item.ref, item.kind,
             item.stamp, thumbnails);

    if (item.ref == m_pinged) {
        const double t = ImGui::GetTime() - m_pingTime;
        if (t < 1.5) {
            const auto alpha = static_cast<int>(255.0 * (1.0 - t / 1.5));
            dl->AddRect(pos, ImVec2(pos.x + cell.x, pos.y + cell.y), IM_COL32(255, 200, 60, alpha), 4.0f, 0, 2.0f);
        } else {
            m_pinged.clear();
        }
    }

    const float labelY = pos.y + m_iconSize + 8.0f;
    if (renaming) {
        ImGui::SetCursorScreenPos(ImVec2(pos.x, labelY - ImGui::GetStyle().FramePadding.y));
        ImGui::SetNextItemWidth(cell.x);
        if (m_renameFocus) {
            ImGui::SetKeyboardFocusHere();
            m_renameFocus = false;
        }
        const bool enter = ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer),
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (enter)
            commitRename();
        else if (ImGui::IsItemDeactivated())
            ImGui::IsKeyPressed(ImGuiKey_Escape) ? m_renaming.clear() : commitRename();
    } else {
        const std::string label = ellipsize(item.label, cell.x - 2.0f);
        const float width = ImGui::CalcTextSize(label.c_str()).x;
        dl->AddText(ImVec2(pos.x + (cell.x - width) * 0.5f, labelY), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
    }
    ImGui::EndGroup();
    ImGui::PopID();
}

void ProjectPanel::drawItemMenu(const Item& item)
{
    switch (item.kind) {
    case AssetKind::Folder:
        if (ImGui::MenuItem("Open"))
            navigate(item.file);
        break;
    case AssetKind::Scene:
        if (ImGui::MenuItem("Open Scene"))
            m_events.openScene = item.ref;
        break;
    case AssetKind::Model:
    case AssetKind::Prefab:
        if (item.kind == AssetKind::Prefab && ImGui::MenuItem("Open Prefab"))
            m_events.openPrefab = item.ref;
        if (ImGui::MenuItem("Add to Scene"))
            m_events.instantiate = item.ref;
        if (item.kind == AssetKind::Model && ImGui::MenuItem("Open in Animation Viewer"))
            m_events.openAnimationViewer = item.ref;
        break;
    case AssetKind::Material:
        break;
    case AssetKind::Animation:
    case AssetKind::Pose:
        if (ImGui::MenuItem("Open in Animation Viewer"))
            m_events.openAnimationViewer = item.ref;
        break;
    default:
        if (ImGui::MenuItem("Open"))
            openExternally(item.file);
        break;
    }
    if (ImGui::MenuItem("Show in File Manager"))
        showInFileManager(item.file);
    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2"))
        beginRename(item.ref);
    if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
        duplicateSelected();
    if (ImGui::MenuItem("Delete", "Del")) {
        m_pendingDelete = item.ref;
        m_openDeletePopup = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Copy Path"))
        ImGui::SetClipboardText(item.ref.c_str());
}

void ProjectPanel::handleShortcuts()
{
    if (!m_focused || ImGui::GetIO().WantTextInput || !m_renaming.empty())
        return;
    const bool ctrl = ImGui::GetIO().KeyCtrl;
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && m_current != assetops::root())
        navigate(m_current.parent_path());
    if (m_selected.empty())
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
        beginRename(m_selected);
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        m_pendingDelete = m_selected;
        m_openDeletePopup = true;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
        duplicateSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        if (const Item* item = findItem(m_selected))
            activate(*item);
    }
}

void ProjectPanel::drawDeletePopup()
{
    if (!ImGui::BeginPopupModal("Delete Asset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    const fs::path file = assetops::fromRef(m_pendingDelete);
    std::error_code ec;
    ImGui::Text("Delete %s?", assetops::displayPath(m_pendingDelete).c_str());
    if (fs::is_directory(file, ec))
        ImGui::TextDisabled("The folder and everything in it will be deleted.");
    ImGui::TextDisabled("This can't be undone.");
    ImGui::Spacing();
    if (ImGui::Button("Delete", ImVec2(100.0f, 0.0f))) {
        std::string error;
        if (assetops::remove(file, &error)) {
            m_events.deleted.push_back(m_pendingDelete);
            if (pathIsUnder(m_selected, m_pendingDelete))
                m_selected.clear();
            if (pathIsUnder(m_current.generic_string(), file.generic_string()))
                m_current = file.parent_path();
            m_rescan = true;
        } else {
            fail("Delete failed: " + error);
        }
        m_pendingDelete.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_pendingDelete.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::EndPopup();
}
