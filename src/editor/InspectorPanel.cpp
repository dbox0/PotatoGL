#include "InspectorPanel.h"

#include "editor/AssetOps.h"
#include "editor/DragDrop.h"
#include "editor/EditCommands.h"
#include "editor/ProjectPanel.h"
#include "editor/ThumbnailCache.h"

#include "engine/assets/AssetCache.h"
#include "engine/reflect/ComponentRegistry.h"
#include "engine/scene/Scene.h"
#include "engine/ui/UiComponents.h"

#include <glm/glm.hpp>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

namespace fs = std::filesystem;

InspectorPanel::Requests InspectorPanel::takeRequests()
{
    return std::exchange(m_requests, {});
}

static bool containsNoCase(const std::string& text, const char* query)
{
    std::string a = text, b = query;
    for (std::string* s : {&a, &b})
        std::transform(s->begin(), s->end(), s->begin(), [](unsigned char c) { return std::tolower(c); });
    return a.find(b) != std::string::npos;
}

bool InspectorPanel::assetSlot(const char* label, AssetKind kind, std::string& value,
                               const std::vector<std::string>& extra)
{
    bool changed = false;
    ImGui::PushID(label);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float pickerWidth = ImGui::GetFrameHeight();
    const float width = std::max(ImGui::CalcItemWidth() - pickerWidth - style.ItemInnerSpacing.x, 20.0f);

    const std::string shown = value.empty() ? std::string("None (") + assetKindName(kind) + ")"
                                            : fs::path(value).filename().string();
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_FrameBg]);
    if (ImGui::Button((shown + "##slot").c_str(), ImVec2(width, 0.0f)) && !value.empty())
        m_requests.ping = value;
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!value.empty())
        ImGui::SetItemTooltip("%s\nClick to show it in the Project view", assetops::displayPath(value).c_str());
    if (ImGui::BeginDragDropTarget()) {
        if (const char* ref = acceptAssetDrop({kind})) {
            value = ref;
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    if (ImGui::Button("o", ImVec2(pickerWidth, 0.0f))) {
        m_pickerItems = extra;
        const std::vector<std::string> files = assetops::findAssets(kind);
        m_pickerItems.insert(m_pickerItems.end(), files.begin(), files.end());
        m_pickerFilter[0] = '\0';
        ImGui::OpenPopup("##picker");
    }
    ImGui::SetItemTooltip("Select %s", assetKindName(kind));
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::TextUnformatted(label);

    if (ImGui::BeginPopup("##picker")) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint("##filter", "Search", m_pickerFilter, sizeof(m_pickerFilter));
        if (ImGui::BeginChild("##items", ImVec2(320.0f, 240.0f))) {
            if (ImGui::Selectable("None", value.empty())) {
                changed = !value.empty();
                value.clear();
                ImGui::CloseCurrentPopup();
            }
            for (const std::string& item : m_pickerItems) {
                const std::string name = assetops::displayPath(item);
                if (m_pickerFilter[0] && !containsNoCase(name, m_pickerFilter))
                    continue;
                if (ImGui::Selectable(name.c_str(), item == value)) {
                    changed = item != value;
                    value = item;
                    ImGui::CloseCurrentPopup();
                }
            }
            if (m_pickerItems.empty())
                ImGui::TextDisabled("No %s assets in the project", assetKindName(kind));
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

// A scene object reference: shows its name, takes drops from the Hierarchy, and has a picker
bool InspectorPanel::entitySlot(const char* label, entt::entity& value)
{
    const entt::registry& registry = m_scene->registry();
    const auto nameOf = [&](entt::entity e) {
        const Name* name = registry.try_get<Name>(e);
        return name && !name->value.empty() ? name->value : std::string("Object");
    };
    const bool valid = value != entt::null && registry.valid(value);
    bool changed = false;
    ImGui::PushID(label);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float pickerWidth = ImGui::GetFrameHeight();
    const float width = std::max(ImGui::CalcItemWidth() - pickerWidth - style.ItemInnerSpacing.x, 20.0f);
    const std::string shown = valid ? nameOf(value) : std::string("None (Object)");
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_FrameBg]);
    ImGui::Button((shown + "##slot").c_str(), ImVec2(width, 0.0f));
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EntityPayload)) {
            entt::entity dropped;
            std::memcpy(&dropped, payload->Data, sizeof(dropped));
            changed = dropped != value;
            value = dropped;
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    if (ImGui::Button("o", ImVec2(pickerWidth, 0.0f))) {
        m_pickerFilter[0] = '\0';
        ImGui::OpenPopup("##entityPicker");
    }
    ImGui::SetItemTooltip("Select an object, or drag one here from the Hierarchy");
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::TextUnformatted(label);

    if (ImGui::BeginPopup("##entityPicker")) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint("##filter", "Search", m_pickerFilter, sizeof(m_pickerFilter));
        if (ImGui::BeginChild("##items", ImVec2(320.0f, 240.0f))) {
            if (ImGui::Selectable("None", !valid)) {
                changed = valid;
                value = entt::null;
                ImGui::CloseCurrentPopup();
            }
            for (const entt::entity e : registry.view<Name>()) {
                if (e == m_scene->root())
                    continue;
                const std::string name = nameOf(e);
                if (m_pickerFilter[0] && !containsNoCase(name, m_pickerFilter))
                    continue;
                ImGui::PushID(static_cast<int>(entt::to_integral(e)));
                if (ImGui::Selectable(name.c_str(), e == value)) {
                    changed = e != value;
                    value = e;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

bool InspectorPanel::drawField(const FieldInfo& field, void* data)
{
    FieldValue value = field.get(data);
    bool changed = false;

    ImGui::BeginDisabled(field.set == nullptr);
    switch (field.type) {
    case FieldType::Float: {
        float v = std::get<float>(value);
        changed = ImGui::DragFloat(field.label.c_str(), &v, field.speed);
        value = v;
        break;
    }
    case FieldType::Int: {
        int v = std::get<int>(value);
        changed = ImGui::DragInt(field.label.c_str(), &v, field.speed);
        value = v;
        break;
    }
    case FieldType::Bool: {
        bool v = std::get<bool>(value);
        changed = ImGui::Checkbox(field.label.c_str(), &v);
        value = v;
        break;
    }
    case FieldType::Vec3: {
        glm::vec3 v = std::get<glm::vec3>(value);
        changed = field.color ? ImGui::ColorEdit3(field.label.c_str(), &v.x, ImGuiColorEditFlags_Float)
                              : ImGui::DragFloat3(field.label.c_str(), &v.x, field.speed);
        value = v;
        break;
    }
    case FieldType::String: {
        if (field.asset != AssetKind::None) {
            std::string v = std::get<std::string>(value);
            changed = assetSlot(field.label.c_str(), field.asset, v,
                                field.asset == AssetKind::Animation ? m_embeddedClips : std::vector<std::string>{});
            value = std::move(v);
            break;
        }
        if (field.joint) {
            const std::string current = std::get<std::string>(value);
            if (ImGui::BeginCombo(field.label.c_str(), current.empty() ? "None" : current.c_str())) {
                if (m_jointNames.empty())
                    ImGui::TextDisabled("The parent has no skinned mesh");
                for (const std::string& name : m_jointNames) {
                    if (ImGui::Selectable(name.c_str(), name == current) && name != current) {
                        value = name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        // Committed on Enter or focus loss, so e.g. a mesh path isn't loaded per keystroke.
        // Only one text field is active at a time; it edits s_editBuffer.
        static char s_editBuffer[2048];
        static ImGuiID s_editId = 0;
        const ImGuiID id = ImGui::GetID(field.label.c_str());
        char local[2048];
        std::snprintf(local, sizeof(local), "%s", std::get<std::string>(value).c_str());
        char* buffer = id == s_editId ? s_editBuffer : local;
        if (field.multiline)
            ImGui::InputTextMultiline(field.label.c_str(), buffer, sizeof(local),
                                      ImVec2(0.0f, ImGui::GetTextLineHeight() * 4.0f + ImGui::GetStyle().FramePadding.y * 2.0f));
        else
            ImGui::InputText(field.label.c_str(), buffer, sizeof(local));
        if (ImGui::IsItemActivated()) {
            std::snprintf(s_editBuffer, sizeof(s_editBuffer), "%s", local);
            s_editId = id;
        }
        if (ImGui::IsItemDeactivated()) {
            changed = ImGui::IsItemDeactivatedAfterEdit();
            value = std::string(s_editBuffer);
            s_editId = 0;
        }
        break;
    }
    case FieldType::Vec2: {
        glm::vec2 v = std::get<glm::vec2>(value);
        changed = ImGui::DragFloat2(field.label.c_str(), &v.x, field.speed);
        value = v;
        break;
    }
    case FieldType::Vec4: {
        glm::vec4 v = std::get<glm::vec4>(value);
        changed = field.color ? ImGui::ColorEdit4(field.label.c_str(), &v.x, ImGuiColorEditFlags_Float)
                              : ImGui::DragFloat4(field.label.c_str(), &v.x, field.speed);
        value = v;
        break;
    }
    case FieldType::Enum: {
        const int current = std::get<int>(value);
        const bool known = current >= 0 && static_cast<std::size_t>(current) < field.options.size();
        const std::string preview =
            known ? nicifyName(field.options[static_cast<std::size_t>(current)]) : std::to_string(current);
        if (ImGui::BeginCombo(field.label.c_str(), preview.c_str())) {
            for (std::size_t i = 0; i < field.options.size(); ++i) {
                const int option = static_cast<int>(i);
                if (ImGui::Selectable(nicifyName(field.options[i]).c_str(), option == current) && option != current) {
                    value = option;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        break;
    }
    case FieldType::Quat: {
        const glm::quat q = std::get<glm::quat>(value);
        ImGui::LabelText(field.label.c_str(), "%.3f %.3f %.3f %.3f", q.x, q.y, q.z, q.w);
        break;
    }
    case FieldType::Entity: {
        entt::entity v = std::get<entt::entity>(value);
        changed = entitySlot(field.label.c_str(), v);
        value = v;
        break;
    }
    }
    ImGui::EndDisabled();

    if (changed && field.set)
        field.set(data, value);
    return changed;
}

void InspectorPanel::draw(Scene& scene, const ComponentRegistry& components, entt::entity selected,
                          EditHistory& history, std::size_t selectionCount, AssetCache& assets,
                          ThumbnailCache& thumbnails, const std::string& asset)
{
    m_history = &history;
    m_assets = &assets;
    m_components = &components;
    m_scene = &scene;
    entt::registry& registry = scene.registry();
    if (ImGui::Begin("Inspector")) {
        if ((selected == entt::null || !registry.valid(selected)) && !asset.empty()) {
            drawAsset(asset, assets, thumbnails);
        } else if (selected == entt::null || !registry.valid(selected)) {
            ImGui::TextDisabled("Nothing selected");
        } else {
            ImGui::TextDisabled("Entity %u", static_cast<unsigned>(entt::to_integral(selected)));
            if (selectionCount > 1)
                ImGui::TextDisabled("%zu objects selected, showing the active one", selectionCount);
            ImGui::PushItemWidth(-90.0f);

            // A RectTransform places UI; its Transform is ignored
            const bool rectTransform = registry.all_of<RectTransform>(selected);
            for (const auto& info : components.all()) {
                if (rectTransform && info->id == "Transform")
                    continue;
                if (!info->isScript && info->has(registry, selected))
                    drawComponent(*info, scene, selected);
            }
            drawAddMenu("Add Component", false, components, scene, selected);

            ImGui::SeparatorText("Scripts");
            for (const auto& info : components.all()) {
                if (info->isScript && info->has(registry, selected))
                    drawComponent(*info, scene, selected);
            }
            drawAddMenu("Add Script", true, components, scene, selected);

            ImGui::PopItemWidth();
        }
    }
    ImGui::End();
    saveMaterial(assets);

    if (selected != entt::null && registry.valid(selected)) {
        EditContext context{scene, components};
        if (m_pendingAdd && !m_pendingAdd->has(registry, selected))
            history.execute(context, addComponentCommand(scene, selected, *m_pendingAdd));
        if (m_pendingRemove && m_pendingRemove->has(registry, selected))
            history.execute(context, removeComponentCommand(scene, selected, *m_pendingRemove));
    }
    m_pendingAdd = nullptr;
    m_pendingRemove = nullptr;
}

void InspectorPanel::drawComponent(const ComponentInfo& info, Scene& scene, entt::entity e)
{
    ImGui::PushID(&info);
    bool keep = true;
    const bool open = ImGui::CollapsingHeader(info.name.c_str(), info.removable ? &keep : nullptr,
                                              ImGuiTreeNodeFlags_DefaultOpen);
    if (!keep)
        m_pendingRemove = &info;

    if (open) {
        entt::registry& registry = scene.registry();
        // Context some fields need: the model's clips, the parent skeleton's joints
        m_embeddedClips.clear();
        m_jointNames.clear();
        if (const SkinnedMeshRenderer* skinned = registry.try_get<SkinnedMeshRenderer>(e);
            skinned && info.id == "Animator" && !skinned->mesh.empty())
            m_embeddedClips = m_assets->clipRefs(skinned->mesh);
        if (info.id == "BoneSocket") {
            const entt::entity parent = registry.get<Relationship>(e).parent;
            const SkinnedMeshRenderer* skinned = parent == entt::null ? nullptr : registry.try_get<SkinnedMeshRenderer>(parent);
            if (skinned && !skinned->mesh.empty()) {
                if (const auto skeleton = m_assets->skeleton(skinned->mesh))
                    m_jointNames = skeleton->names;
            }
        }
        if (info.id == "RectTransform" || info.id == "Canvas") {
            if (info.id == "RectTransform") {
                drawAnchorPresets(info, scene, e);
                ImGui::SameLine();
            }
            if (ImGui::Button("Edit in Canvas"))
                m_requests.editCanvas = true;
        }
        const bool driven = info.id == "Transform" && registry.all_of<BoneSocket>(e);
        if (driven)
            ImGui::TextDisabled("Driven by the Bone Socket");
        ImGui::BeginDisabled(driven);
        void* data = info.get(scene.registry(), e);
        if (!data)
            ImGui::TextDisabled("Tag");
        else if (info.fields.empty())
            ImGui::TextDisabled("No editable fields");
        else
            for (const FieldInfo& field : info.fields) {
                if (!field.visible)
                    continue;
                // Own ID scope: a field may share its label with the header ("Name")
                ImGui::PushID(field.key.c_str());
                const FieldValue before = field.get(data);
                const bool changed = drawField(field, data);
                if (field.asset != AssetKind::None || field.joint || field.type == FieldType::Enum ||
                    field.type == FieldType::Entity) {
                    // Drops and picks are complete edits as they happen
                    if (changed)
                        m_history->push(setFieldCommand(scene, e, info, field, before, field.get(data)));
                } else if (ImGui::IsItemActivated())
                    m_editStart = before;
                if (ImGui::IsItemDeactivated() && m_editStart) {
                    const FieldValue after = field.get(data);
                    if (ImGui::IsItemDeactivatedAfterEdit() && after != *m_editStart)
                        m_history->push(setFieldCommand(scene, e, info, field, *m_editStart, after));
                    m_editStart.reset();
                }
                ImGui::PopID();
            }
        ImGui::EndDisabled();
        if (info.id == "SkinnedMeshRenderer") {
            const SkinnedMeshRenderer& skinned = registry.get<SkinnedMeshRenderer>(e);
            ImGui::BeginDisabled(!skinned.skeleton);
            if (ImGui::Button("Open in Animation Viewer", ImVec2(-1.0f, 0.0f))) {
                const Animator* animator = registry.try_get<Animator>(e);
                m_requests.openAnimationViewer =
                    Requests::Viewer{skinned.mesh, skinned.material, animator ? animator->clip : std::string()};
            }
            ImGui::EndDisabled();
        }
    }
    ImGui::PopID();
}

// Unity's 4x4 grid: left, centre, right, stretch by top, middle, bottom, stretch (y down).
// The rect stays where it is; Shift also sets the pivot, Alt also moves it onto the anchors.
void InspectorPanel::drawAnchorPresets(const ComponentInfo& info, Scene& scene, entt::entity e)
{
    static const glm::vec2 Ranges[4] = {{0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    entt::registry& registry = scene.registry();
    RectTransform& rt = registry.get<RectTransform>(e);
    if (ImGui::Button("Anchor Presets"))
        ImGui::OpenPopup("##anchorPresets");
    if (!ImGui::BeginPopup("##anchorPresets"))
        return;
    ImGui::TextDisabled("Shift: also set pivot   Alt: also set position");
    const ImGuiIO& io = ImGui::GetIO();
    constexpr float cell = 38.0f;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            const glm::vec2 h = Ranges[col], v = Ranges[row];
            ImGui::PushID(row * 4 + col);
            if (col > 0)
                ImGui::SameLine();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("##preset", ImVec2(cell, cell));
            const bool current = rt.anchorMin == glm::vec2(h.x, v.x) && rt.anchorMax == glm::vec2(h.y, v.y);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell),
                                ImGui::IsItemHovered() ? IM_COL32(80, 80, 90, 255) : IM_COL32(50, 50, 55, 255), 3.0f);
            if (current)
                draw->AddRect(p, ImVec2(p.x + cell, p.y + cell), IM_COL32(60, 150, 255, 255), 3.0f, 0, 2.0f);
            const ImVec2 lo(p.x + 7.0f, p.y + 7.0f), hi(p.x + cell - 7.0f, p.y + cell - 7.0f);
            draw->AddRect(lo, hi, IM_COL32(150, 150, 150, 255));
            const auto span = [](float from, float to, glm::vec2 range) {
                constexpr float box = 8.0f;
                if (range.x != range.y)
                    return glm::vec2(from, to);
                const float at = from + (to - from) * range.x;
                return glm::vec2(std::clamp(at - box * 0.5f, from, to - box), std::clamp(at + box * 0.5f, from + box, to));
            };
            const glm::vec2 xs = span(lo.x, hi.x, h), ys = span(lo.y, hi.y, v);
            draw->AddRectFilled(ImVec2(xs.x + 2.0f, ys.x + 2.0f), ImVec2(xs.y - 2.0f, ys.y - 2.0f), IM_COL32(220, 90, 90, 255));
            if (clicked) {
                const RectTransform before = rt;
                const glm::vec2 anchorMin(h.x, v.x), anchorMax(h.y, v.y);
                glm::vec2 pivot = rt.pivot;
                if (io.KeyShift)
                    pivot = glm::vec2(h.x == h.y ? h.x : 0.5f, v.x == v.y ? v.x : 0.5f);
                const UIRect* rect = registry.try_get<UIRect>(e);
                const UIRect* parentRect = registry.try_get<UIRect>(scene.parentOf(e));
                if (rect && parentRect) {
                    const glm::vec2 size = rect->max - rect->min;
                    const glm::vec2 parentSize = parentRect->max - parentRect->min;
                    const glm::vec2 aMin = parentRect->min + anchorMin * parentSize;
                    const glm::vec2 aMax = parentRect->min + anchorMax * parentSize;
                    rt.sizeDelta = size - (aMax - aMin);
                    rt.anchoredPosition = rect->min + size * pivot - (aMin + (aMax - aMin) * pivot);
                }
                rt.anchorMin = anchorMin;
                rt.anchorMax = anchorMax;
                rt.pivot = pivot;
                if (io.KeyAlt) {
                    rt.anchoredPosition = glm::vec2(0.0f);
                    if (h.x != h.y)
                        rt.sizeDelta.x = 0.0f;
                    if (v.x != v.y)
                        rt.sizeDelta.y = 0.0f;
                }
                EditContext context{scene, *m_components};
                if (auto command = componentEditCommand(context, e, info, &before, &rt, "Anchor Preset"))
                    m_history->push(std::move(command));
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndPopup();
}

void InspectorPanel::drawAddMenu(const char* button, bool scripts, const ComponentRegistry& components,
                                 Scene& scene, entt::entity e)
{
    ImGui::Spacing();
    if (ImGui::Button(button, ImVec2(-1.0f, 0.0f)))
        ImGui::OpenPopup(button);
    if (ImGui::BeginPopup(button)) {
        bool any = false;
        for (const auto& info : components.all()) {
            if (info->isScript != scripts || !info->addable || info->has(scene.registry(), e))
                continue;
            any = true;
            if (ImGui::MenuItem(info->name.c_str()))
                m_pendingAdd = info.get();
        }
        if (!any)
            ImGui::TextDisabled("Nothing to add");
        ImGui::EndPopup();
    }
}

// Thumbnail or a plain frame, size x size
static void drawPreview(ThumbnailCache& thumbnails, const std::string& ref, AssetKind kind, float size)
{
    std::error_code ec;
    const auto stamp = fs::last_write_time(assetops::fromRef(ref), ec);
    const ThumbnailCache::Image image = thumbnails.get(ref, kind, stamp);
    if (!image) {
        ImGui::Dummy(ImVec2(size, size));
        return;
    }
    const float scale = size / static_cast<float>(std::max(image.width, image.height));
    const ImVec2 extent(static_cast<float>(image.width) * scale, static_cast<float>(image.height) * scale);
    const auto texture = static_cast<ImTextureID>(image.texture);
    if (image.flipY)
        ImGui::Image(texture, extent, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    else
        ImGui::Image(texture, extent);
}

void InspectorPanel::drawAsset(const std::string& ref, AssetCache& assets, ThumbnailCache& thumbnails)
{
    const fs::path file = assetops::fromRef(ref);
    const AssetKind kind = assetops::kindOf(file);
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        ImGui::TextDisabled("Nothing selected");
        return;
    }

    const std::string name = ref == std::string(AssetsFolder) ? std::string("Assets") : file.filename().string();
    ImGui::TextUnformatted(name.c_str());
    ImGui::TextDisabled("%s  -  %s", assetKindName(kind), assetops::displayPath(ref).c_str());
    ImGui::Separator();

    const float previewSize = std::min(ImGui::GetContentRegionAvail().x, 256.0f);
    switch (kind) {
    case AssetKind::Material:
        drawMaterial(ref, assets, thumbnails);
        break;
    case AssetKind::Texture: {
        drawPreview(thumbnails, ref, kind, previewSize);
        std::error_code stampError;
        const ThumbnailCache::Image image = thumbnails.get(ref, kind, fs::last_write_time(file, stampError));
        if (image)
            ImGui::Text("%d x %d", image.sourceWidth, image.sourceHeight);
        ImGui::TextDisabled("Imported as sRGB color, scaled to the texture array's layer size");
        ImGui::TextDisabled("Sprites and UI images use it at full size");

        ImGui::SeparatorText("Sprite");
        SpriteMeta meta = assets.spriteMeta(ref);
        bool changed = ImGui::DragFloat4("Border", &meta.border.x, 0.5f, 0.0f, 8192.0f, "%.0f");
        bool commit = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("Left, top, right, bottom, in pixels; Sliced UI images keep them unscaled");
        changed |= ImGui::DragFloat("Pixels Per Unit", &meta.pixelsPerUnit, 1.0f, 1.0f, 10000.0f, "%.0f");
        commit |= ImGui::IsItemDeactivatedAfterEdit();
        int filter = static_cast<int>(meta.filter);
        if (ImGui::Combo("Filter", &filter, "Linear\0Point\0")) {
            meta.filter = static_cast<SpriteFilter>(filter);
            changed = commit = true;
        }
        meta.pixelsPerUnit = std::max(meta.pixelsPerUnit, 1.0f);
        if (changed || commit)
            assets.setSpriteMeta(ref, meta, commit);
        break;
    }
    case AssetKind::Model:
    case AssetKind::Prefab:
        if (kind == AssetKind::Model)
            drawPreview(thumbnails, ref, kind, previewSize);
        if (kind == AssetKind::Prefab && ImGui::Button("Open Prefab", ImVec2(-1.0f, 0.0f)))
            m_requests.openPrefab = ref;
        if (kind == AssetKind::Model && assets.skeleton(ref) &&
            ImGui::Button("Open in Animation Viewer", ImVec2(-1.0f, 0.0f)))
            m_requests.openAnimationViewer = Requests::Viewer{ref, {}, {}};
        if (ImGui::Button("Add to Scene", ImVec2(-1.0f, 0.0f)))
            m_requests.instantiate = ref;
        ImGui::TextDisabled("Or drag it into the Scene view or the Hierarchy");
        break;
    case AssetKind::Scene:
        if (ImGui::Button("Open Scene", ImVec2(-1.0f, 0.0f)))
            m_requests.openScene = ref;
        break;
    case AssetKind::Folder:
        break;
    default: {
        if (ImGui::Button("Open", ImVec2(-1.0f, 0.0f)))
            openExternally(file);
        if (m_textRef != ref) {
            m_textRef = ref;
            m_text = assetops::readText(file, 32 * 1024).value_or("");
            if (m_text.find('\0') != std::string::npos)
                m_text = "(binary file)";
        }
        if (ImGui::BeginChild("##text", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar))
            ImGui::TextUnformatted(m_text.c_str(), m_text.c_str() + m_text.size());
        ImGui::EndChild();
        break;
    }
    }
}

void InspectorPanel::drawMaterial(const std::string& ref, AssetCache& assets, ThumbnailCache& thumbnails)
{
    const MaterialAsset* current = assets.materialAsset(ref);
    if (!current) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Can't read this material");
        return;
    }
    MaterialAsset m = *current;
    bool changed = false;

    ImGui::PushItemWidth(-110.0f);
    ImGui::TextDisabled("Shader: Lit");
    ImGui::SeparatorText("Surface");
    changed |= assetSlot("Base Map", AssetKind::Texture, m.baseMap);
    changed |= ImGui::ColorEdit3("Base Color", &m.baseColor.x);
    changed |= ImGui::SliderFloat("Metallic", &m.metallic, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Smoothness", &m.smoothness, 0.0f, 1.0f);
    changed |= ImGui::DragFloat2("Tiling", &m.tiling.x, 0.01f);
    changed |= ImGui::DragFloat2("Offset", &m.offset.x, 0.01f);
    ImGui::SeparatorText("Emission");
    changed |= ImGui::ColorEdit3("Emission Color", &m.emission.x);
    changed |= ImGui::DragFloat("Intensity", &m.emissionIntensity, 0.02f, 0.0f, 100.0f);
    ImGui::PopItemWidth();

    if (changed && m != *current) {
        assets.setMaterialAsset(ref, m);
        thumbnails.invalidate(ref);
        if (!m_unsavedMaterial.empty() && m_unsavedMaterial != ref)
            saveMaterial(assets);
        m_unsavedMaterial = ref;
    }

    ImGui::SeparatorText("Preview");
    drawPreview(thumbnails, ref, AssetKind::Material, std::min(ImGui::GetContentRegionAvail().x, 256.0f));
}

// Live edits go to the renderer at once; the file is written when the drag or pick ends
void InspectorPanel::saveMaterial(AssetCache& assets)
{
    if (m_unsavedMaterial.empty() || ImGui::IsAnyItemActive())
        return;
    const std::string ref = std::exchange(m_unsavedMaterial, {});
    const MaterialAsset* material = assets.materialAsset(ref);
    std::string error;
    if (material && !saveMaterialFile(assetops::fromRef(ref).string(), *material, &error))
        m_requests.errors.push_back("Can't save material: " + error);
}
