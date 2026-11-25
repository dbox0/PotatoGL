#include "InspectorPanel.h"

#include "editor/EditorLayout.h"

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
        changed = ImGui::DragFloat(field.name, &v, field.speed);
        value = v;
        break;
    }
    case FieldType::Int: {
        int v = std::get<int>(value);
        changed = ImGui::DragInt(field.name, &v, field.speed);
        value = v;
        break;
    }
    case FieldType::Bool: {
        bool v = std::get<bool>(value);
        changed = ImGui::Checkbox(field.name, &v);
        value = v;
        break;
    }
    case FieldType::Vec3: {
        glm::vec3 v = std::get<glm::vec3>(value);
        changed = ImGui::DragFloat3(field.name, &v.x, field.speed);
        value = v;
        break;
    }
    case FieldType::String: {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s", std::get<std::string>(value).c_str());
        changed = ImGui::InputText(field.name, buffer, sizeof(buffer));
        value = std::string(buffer);
        break;
    }
    case FieldType::Entity: {
        const entt::entity v = std::get<entt::entity>(value);
        if (v == entt::null)
            ImGui::LabelText(field.name, "None");
        else
            ImGui::LabelText(field.name, "Entity %u", static_cast<unsigned>(entt::to_integral(v)));
        break;
    }
    }
    ImGui::EndDisabled();

    if (changed && field.set)
        field.set(data, value);
    return changed;
}

void InspectorPanel::draw(Scene& scene, const ComponentRegistry& components, entt::entity selected)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float top = viewport->WorkSize.y * HierarchyHeightFraction;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + top),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(SidebarWidth, viewport->WorkSize.y - top), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    entt::registry& registry = scene.registry();
    if (ImGui::Begin("Inspector", nullptr, flags)) {
        if (selected == entt::null || !registry.valid(selected)) {
            ImGui::TextDisabled("Nothing selected");
        } else {
            ImGui::TextDisabled("Entity %u", static_cast<unsigned>(entt::to_integral(selected)));
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
        if (m_pendingAdd && !m_pendingAdd->has(registry, selected))
            m_pendingAdd->add(registry, selected);
        if (m_pendingRemove && m_pendingRemove->has(registry, selected))
            m_pendingRemove->remove(registry, selected);
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
            for (const FieldInfo& field : info.fields)
                drawField(field, data);
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
