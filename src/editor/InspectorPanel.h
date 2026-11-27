#pragma once

#include "engine/assets/AssetTypes.h"
#include "engine/reflect/ComponentRegistry.h"

#include <entt/entity/entity.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

class AssetCache;
class EditHistory;
class Scene;
class ThumbnailCache;

// Shows the active object's components, or the selected Project asset when no object is
// selected. Materials are edited here and saved as soon as a widget is let go.
class InspectorPanel
{
public:
    // Asked for by buttons in the inspector; the editor carries them out
    struct Requests
    {
        std::string ping;         // show this asset in the Project view
        std::string openScene;
        std::string openPrefab;
        std::string instantiate;  // model or prefab
        std::vector<std::string> errors;
    };

private:
    // Applied after drawing so no widget is holding a pointer into a removed component
    const ComponentInfo* m_pendingAdd = nullptr;
    const ComponentInfo* m_pendingRemove = nullptr;
    // Value when the active widget was grabbed; the edit is recorded when it's let go
    std::optional<FieldValue> m_editStart;
    EditHistory* m_history = nullptr;  // valid during draw()

    char m_pickerFilter[128] = {};
    std::vector<std::string> m_pickerItems;
    std::string m_unsavedMaterial;  // edited, saved once no widget is active
    std::string m_textRef;          // file whose text preview is cached
    std::string m_text;
    Requests m_requests;

    bool drawField(const FieldInfo& field, void* data);
    void drawComponent(const ComponentInfo& info, Scene& scene, entt::entity e);
    void drawAddMenu(const char* button, bool scripts, const ComponentRegistry& components, Scene& scene,
                     entt::entity e);
    void drawAsset(const std::string& ref, AssetCache& assets, ThumbnailCache& thumbnails);
    void drawMaterial(const std::string& ref, AssetCache& assets, ThumbnailCache& thumbnails);
    void saveMaterial(AssetCache& assets);

public:
    // Object field and material map widget: shows the asset's name, takes drops from the
    // Project view, and has a picker listing every asset of kind. True when value changed.
    bool assetSlot(const char* label, AssetKind kind, std::string& value);

    // selected: the active object; selectionCount: how many are selected in all.
    // asset: the Project view's selection, shown when no object is selected.
    void draw(Scene& scene, const ComponentRegistry& components, entt::entity selected, EditHistory& history,
              std::size_t selectionCount, AssetCache& assets, ThumbnailCache& thumbnails, const std::string& asset);
    Requests takeRequests();
};
