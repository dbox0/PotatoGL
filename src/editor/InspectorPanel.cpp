#include "InspectorPanel.h"

#include "editor/EditCommands.h"

#include "engine/reflect/ComponentRegistry.h"
#include "engine/scene/Scene.h"

#include <imgui.h>

#include <cstdio>

static bool drawField(const FieldInfo& field, void* data)
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
        changed = ImGui::DragFloat3(field.label.c_str(), &v.x, field.speed);
        value = v;
        break;
    }
    case FieldType::String: {
        // Committed on Enter or focus loss, so e.g. a mesh path isn't loaded per keystroke.
        // Only one text field is active at a time; it edits s_editBuffer.
        static char s_editBuffer[256];
        static ImGuiID s_editId = 0;
        const ImGuiID id = ImGui::GetID(field.label.c_str());
        char local[256];
        std::snprintf(local, sizeof(local), "%s", std::get<std::string>(value).c_str());
        char* buffer = id == s_editId ? s_editBuffer : local;
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
    case FieldType::Quat: {
        const glm::quat q = std::get<glm::quat>(value);
        ImGui::LabelText(field.label.c_str(), "%.3f %.3f %.3f %.3f", q.x, q.y, q.z, q.w);
        break;
    }
    case FieldType::Entity: {
        const entt::entity v = std::get<entt::entity>(value);
        if (v == entt::null)
            ImGui::LabelText(field.label.c_str(), "None");
        else
            ImGui::LabelText(field.label.c_str(), "Entity %u", static_cast<unsigned>(entt::to_integral(v)));
        break;
    }
    }
    ImGui::EndDisabled();

    if (changed && field.set)
        field.set(data, value);
    return changed;
}

void InspectorPanel::draw(Scene& scene, const ComponentRegistry& components, entt::entity selected,
                          EditHistory& history, std::size_t selectionCount)
{
    m_history = &history;
    entt::registry& registry = scene.registry();
    if (ImGui::Begin("Inspector")) {
        if (selected == entt::null || !registry.valid(selected)) {
            ImGui::TextDisabled("Nothing selected");
        } else {
            ImGui::TextDisabled("Entity %u", static_cast<unsigned>(entt::to_integral(selected)));
            if (selectionCount > 1)
                ImGui::TextDisabled("%zu objects selected, showing the active one", selectionCount);
            ImGui::PushItemWidth(-90.0f);

            for (const auto& info : components.all()) {
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
                drawField(field, data);
                if (ImGui::IsItemActivated())
                    m_editStart = before;
                if (ImGui::IsItemDeactivated() && m_editStart) {
                    const FieldValue after = field.get(data);
                    if (ImGui::IsItemDeactivatedAfterEdit() && after != *m_editStart)
                        m_history->push(setFieldCommand(scene, e, info, field, *m_editStart, after));
                    m_editStart.reset();
                }
                ImGui::PopID();
            }
    }
    ImGui::PopID();
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
