#include "DebugUi.h"

#include "app/CameraController.h"
#include "render/Renderer.h"
#include "scene/Camera.h"

#include <glad/gl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <cstdio>

DebugUi::~DebugUi()
{
    shutdown();
}

bool DebugUi::init(SDL_Window* window, SDL_GLContext context)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForOpenGL(window, context) || !ImGui_ImplOpenGL3_Init("#version 430 core")) {
        ImGui::DestroyContext();
        return false;
    }
    m_initialized = true;
    m_glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    m_glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    return true;
}

void DebugUi::shutdown()
{
    if (!m_initialized)
        return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void DebugUi::processEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1 && !event.key.repeat)
        m_visible = !m_visible;
    ImGui_ImplSDL3_ProcessEvent(&event);
}

bool DebugUi::wantsMouse() const
{
    return m_visible && ImGui::GetIO().WantCaptureMouse;
}

bool DebugUi::wantsKeyboard() const
{
    return m_visible && ImGui::GetIO().WantCaptureKeyboard;
}

void DebugUi::beginFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
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

void DebugUi::draw(Renderer& renderer, Camera& camera, CameraController& controller)
{
    if (!m_visible)
        return;

    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug (F1)")) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("%.1f FPS  %.2f ms", io.Framerate, 1000.0f / io.Framerate);
        ImGui::TextDisabled("%s", m_glRenderer.c_str());
        ImGui::TextDisabled("GL %s", m_glVersion.c_str());
        if (ImGui::Checkbox("VSync", &m_vsync))
            SDL_GL_SetSwapInterval(m_vsync ? 1 : 0);

        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
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
            ImGui::Text("%zu models, %lld draws in 1 call", s.models, static_cast<long long>(s.draws));
            usageRow("Vertices", s.vertices, s.vertexCapacity);
            usageRow("Indices", s.indices, s.indexCapacity);
            usageRow("Draws", s.draws, s.drawCapacity);
            usageRow("Tex layers", s.textureLayers, s.textureLayerCapacity);
            ImGui::TextDisabled("Layer size %dx%d", s.textureSize, s.textureSize);
        }

        if (ImGui::CollapsingHeader("Shaders", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Auto reload", &m_autoReload);
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

void DebugUi::render()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
