#pragma once

#include "engine/assets/AssetTypes.h"

#include <imgui.h>

#include <initializer_list>
#include <string>

// ImGui drag and drop payload types shared between panels
inline constexpr const char* EntityPayload = "SCENE_ENTITY";  // entt::entity
inline constexpr const char* AssetPayload = "ASSET_PATH";     // resource-relative path, null terminated

inline void setAssetPayload(const std::string& ref)
{
    ImGui::SetDragDropPayload(AssetPayload, ref.c_str(), ref.size() + 1);
}

// The dropped asset's path if it's a file of one of kinds, else nullptr. Call between
// Begin/EndDragDropTarget. Other kinds aren't accepted, so the target doesn't highlight.
inline const char* acceptAssetDrop(std::initializer_list<AssetKind> kinds)
{
    const ImGuiPayload* current = ImGui::GetDragDropPayload();
    if (!current || !current->IsDataType(AssetPayload))
        return nullptr;
    const AssetKind kind = assetKindOf(static_cast<const char*>(current->Data));
    bool wanted = false;
    for (const AssetKind k : kinds)
        wanted = wanted || k == kind;
    if (!wanted)
        return nullptr;
    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetPayload);
    return payload ? static_cast<const char*>(payload->Data) : nullptr;
}
