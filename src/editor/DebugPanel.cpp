#include "DebugPanel.h"

#include "engine/app/CameraController.h"
#include "engine/app/Overlay.h"
#include "engine/render/Renderer.h"
#include "engine/render/UIRenderer.h"
#include "engine/scene/Camera.h"

#include <glad/gl.h>
#include <imgui.h>

#include <cstdio>

void DebugPanel::init()
{
    m_glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    m_glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
}

static void usageRow(const char* label, long long used, long long capacity)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(110.0f);
    const float fraction = capacity > 0 ? static_cast<float>(used) / static_cast<float>(capacity) : 0.0f;
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%lld / %lld", used, capacity);
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
}

void DebugPanel::draw(const OverlayContext& context)
{
    Renderer& renderer = context.renderer;
    Camera& camera = context.camera;
    CameraController& controller = context.controller;

    if (ImGui::Begin("Debug")) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("%.1f FPS  %.2f ms", io.Framerate, 1000.0f / io.Framerate);
        ImGui::TextDisabled("%s", m_glRenderer.c_str());
        ImGui::TextDisabled("GL %s", m_glVersion.c_str());
        if (ImGui::Checkbox("VSync", &m_vsync))
            SDL_GL_SetSwapInterval(m_vsync ? 1 : 0);

        if (ImGui::CollapsingHeader("Editor Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            const glm::vec3 pos = camera.getTransform().getPos();
            ImGui::Text("Position %.2f %.2f %.2f", pos.x, pos.y, pos.z);
            float fov = camera.getFovDeg();
            if (ImGui::SliderFloat("FOV", &fov, 20.0f, 120.0f, "%.0f deg"))
                camera.setFov(fov);
            float speed = controller.getMoveSpeed();
            if (ImGui::SliderFloat("Speed", &speed, 0.5f, 50.0f, "%.1f", ImGuiSliderFlags_Logarithmic))
                controller.setMoveSpeed(speed);
        }

        if (ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
            const Renderer::Stats s = renderer.stats();
            ImGui::Text("%zu models, %lld instances, %lld draws in 1 call", s.models,
                        static_cast<long long>(s.instances), static_cast<long long>(s.draws));
            usageRow("Vertices", s.vertices, s.vertexCapacity);
            usageRow("Indices", s.indices, s.indexCapacity);
            usageRow("Draws", s.draws, s.drawCapacity);
            usageRow("Tex layers", s.textureLayers, s.textureLayerCapacity);
            ImGui::TextDisabled("Layer size %dx%d", s.textureSize, s.textureSize);
            ImGui::Text("%zu lights, %zu cluster refs", s.lights, s.clusterRefs);

            bool prepass = renderer.depthPrepass();
            if (ImGui::Checkbox("Depth prepass", &prepass))
                renderer.setDepthPrepass(prepass);
            Renderer::ShadowSettings& shadows = renderer.shadowSettings();
            ImGui::Checkbox("Shadows", &shadows.enabled);
            ImGui::SliderFloat("Shadow distance", &shadows.distance, 5.0f, 100.0f, "%.0f m");
        }

        if (ImGui::CollapsingHeader("UI", ImGuiTreeNodeFlags_DefaultOpen)) {
            const UIRenderer::Stats ui = context.ui.frameStats();
            ImGui::Text("%zu vertices, %zu batches", ui.vertices, ui.batches);
            ImGui::TextDisabled("Every UI view of the last frame");
        }

        if (ImGui::CollapsingHeader("Shaders", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool autoReload = renderer.shaderAutoReload();
            if (ImGui::Checkbox("Auto reload", &autoReload))
                renderer.setShaderAutoReload(autoReload);
            ImGui::SameLine();
            if (ImGui::Button("Reload now"))
                renderer.reloadShaders();
            if (!renderer.shaderError().empty())
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", renderer.shaderError().c_str());
        }

        ImGui::Checkbox("ImGui demo", &m_showDemo);
    }
    ImGui::End();

    if (m_showDemo)
        ImGui::ShowDemoWindow(&m_showDemo);
}
