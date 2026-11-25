#include "App.h"

#include "assets/Model.h"
#include "core/Paths.h"
#include "scene/Components.h"
#include "scene/Transform.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>

#ifndef NDEBUG
static void GLAD_API_PTR glDebugCallback(GLenum, GLenum type, GLuint, GLenum severity,
                                         GLsizei, const GLchar* message, const void*)
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
        return;
    std::fprintf(stderr, "[GL %s] %s\n", type == GL_DEBUG_TYPE_ERROR ? "ERROR" : "debug", message);
}
#endif

App::~App()
{
    m_debugUi.shutdown();  // GL objects must die before the context
    m_renderer.reset();
    if (m_context)
        SDL_GL_DestroyContext(m_context);
    if (m_window)
        SDL_DestroyWindow(m_window);
    SDL_Quit();
}

bool App::init(const char* title, int width, int height)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "Failed to init SDL: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
#ifndef NDEBUG
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

    m_window = SDL_CreateWindow(title, width, height,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!m_window) {
        std::fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        return false;
    }
    m_context = SDL_GL_CreateContext(m_window);
    if (!m_context) {
        std::fprintf(stderr, "Failed to create GL 4.3 context: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(m_window, m_context);
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))) {
        std::fprintf(stderr, "Failed to load OpenGL\n");
        return false;
    }
    std::printf("GL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

#ifndef NDEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(glDebugCallback, nullptr);
#endif

    m_renderer = Renderer::create();
    if (!m_renderer)
        return false;
    if (!m_debugUi.init(m_window, m_context)) {
        std::fprintf(stderr, "Failed to init Dear ImGui\n");
        return false;
    }

    int fbWidth, fbHeight;
    SDL_GetWindowSizeInPixels(m_window, &fbWidth, &fbHeight);
    onResize(fbWidth, fbHeight);

    return buildDemoScene();
}

bool App::buildDemoScene()
{
    std::optional<ModelData> duck = loadModel(resourcePath("assets/Duck.glb"));
    if (!duck)
        return false;
    const ModelId duckModel = m_renderer->addModel(*duck);

    constexpr int gridSize = 10;
    constexpr float spacing = 2.5f;
    for (int z = 0; z < gridSize; ++z) {
        for (int x = 0; x < gridSize; ++x) {
            const glm::vec3 pos((x - (gridSize - 1) * 0.5f) * spacing, 0.0f, (z - (gridSize - 1) * 0.5f) * spacing);
            const entt::entity duckObject = createGameObject("Duck", duckModel, pos);
            const float yaw = glm::radians(static_cast<float>((x * 37 + z * 61) % 360));
            m_registry.get<Transform>(duckObject).rotate(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }

    m_camera.getTransform().setPos({0.0f, 12.0f, 22.0f});
    m_camera.getTransform().lookAt({0.0f, 0.0f, 0.0f});
    m_controller.syncFrom(m_camera);
    return true;
}

entt::entity App::createGameObject(std::string name, ModelId model, const glm::vec3& pos)
{
    const entt::entity entity = m_registry.create();
    m_registry.emplace<Name>(entity, std::move(name));
    m_registry.emplace<Transform>(entity).setPos(pos);
    m_registry.emplace<MeshRenderer>(entity, model);
    return entity;
}

// Rebuilds every instance each frame until the Renderer gets handle-based instances
void App::syncRenderInstances()
{
    m_renderer->clearInstances();
    for (auto [entity, transform, mesh] : m_registry.view<Transform, MeshRenderer>().each())
        m_renderer->addInstance(mesh.model, transform.getMatrix());
}

void App::onResize(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    m_renderer->setViewport(width, height);
    m_camera.setAspect(static_cast<float>(width) / static_cast<float>(height));
}

void App::handleEvent(const SDL_Event& event)
{
    m_debugUi.processEvent(event);
    const bool clickOnUi = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && m_debugUi.wantsMouse();
    if (!clickOnUi)
        m_controller.handleEvent(event, m_window);

    if (event.type == SDL_EVENT_QUIT)
        m_running = false;
    else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
        m_running = false;
    else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        onResize(event.window.data1, event.window.data2);
}

void App::run()
{
    m_running = true;
    Uint64 lastTicks = SDL_GetTicksNS();
    while (m_running) {
        const Uint64 now = SDL_GetTicksNS();
        const float dt = static_cast<float>(now - lastTicks) * 1e-9f;
        lastTicks = now;

        SDL_Event event;
        while (SDL_PollEvent(&event))
            handleEvent(event);

        m_debugUi.beginFrame();
        m_debugUi.draw(*m_renderer, m_camera, m_controller);

        m_controller.setKeyboardEnabled(!m_debugUi.wantsKeyboard());
        m_controller.update(dt, m_camera);
        if (m_debugUi.autoReloadShaders())
            m_renderer->pollShaderChanges();

        syncRenderInstances();
        m_renderer->render(m_camera);
        m_debugUi.render();
        SDL_GL_SwapWindow(m_window);
    }
}
