#include "App.h"

#include "engine/core/Input.h"
#include "engine/scene/ComponentReflection.h"

#include <glad/gl.h>

#include <cstdio>
#include <optional>
#include <utility>

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
    if (m_game && m_renderer)
        m_game->shutdown(gameContext());
    m_game.reset();
    if (m_overlay)
        m_overlay->shutdown();  // GL objects must die before the context
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
    m_assets.setRenderer(m_renderer.get());
    if (m_overlay && !m_overlay->init(m_window, m_context)) {
        std::fprintf(stderr, "Failed to init overlay\n");
        return false;
    }

    int fbWidth, fbHeight;
    SDL_GetWindowSizeInPixels(m_window, &fbWidth, &fbHeight);
    onResize(fbWidth, fbHeight);

    m_playMode = m_overlay ? PlayMode::Edit : PlayMode::Play;
    if (m_overlay)
        m_controller.setLookButtons(SDL_BUTTON_RMASK);  // left click belongs to picking and gizmos
    m_scene.registry().ctx().emplace<Input>();
    registerSceneComponents(m_components);
    if (m_game && !m_game->init(gameContext()))
        return false;
    m_controller.syncFrom(m_camera);
    return true;
}

// Rebuilds every instance each frame until the Renderer gets handle-based instances
void App::syncRenderInstances()
{
    entt::registry& registry = m_scene.registry();
    const auto* selected = std::as_const(registry).storage<Selected>();
    const bool anySelected = selected && !selected->empty();

    m_renderer->clearInstances();
    for (auto [entity, world, mesh] : registry.view<WorldTransform, MeshRenderer>().each()) {
        if (mesh.model == InvalidModel) {
            if (mesh.mesh.empty())
                continue;
            mesh.model = m_assets.model(mesh.mesh);
            if (mesh.model == InvalidModel)
                continue;
        }
        bool highlight = false;
        if (anySelected) {
            for (entt::entity e = entity; e != entt::null && !highlight; e = registry.get<Relationship>(e).parent)
                highlight = selected->contains(e);
        }
        m_renderer->addInstance(mesh.model, world.matrix, entt::to_integral(entity), highlight);
    }
}

// Lights point down their local -Z, like cameras
void App::syncRenderLights()
{
    const entt::registry& registry = m_scene.registry();
    m_renderer->clearLights();

    auto suns = registry.view<WorldTransform, DirectionalLight>();
    if (const auto it = suns.begin(); it != suns.end()) {
        const auto& [world, light] = suns.get<WorldTransform, DirectionalLight>(*it);
        m_renderer->setDirectionalLight(glm::vec3(world.matrix[2]), light.color, light.intensity);
    }
    registry.view<WorldTransform, PointLight>().each([&](const WorldTransform& world, const PointLight& light) {
        m_renderer->addPointLight(glm::vec3(world.matrix[3]), light.color, light.intensity, light.range);
    });
    registry.view<WorldTransform, SpotLight>().each([&](const WorldTransform& world, const SpotLight& light) {
        m_renderer->addSpotLight(glm::vec3(world.matrix[3]), -glm::vec3(world.matrix[2]), light.color,
                                 light.intensity, light.range, light.innerDeg, light.outerDeg);
    });
}

// Without a game camera the editor camera stands in
void App::renderToWindow()
{
    const float aspect = static_cast<float>(m_fbWidth) / static_cast<float>(m_fbHeight);
    std::optional<ViewMatrices> view = m_scene.gameCameraView(aspect);
    if (!view) {
        m_camera.setAspect(aspect);
        view = ViewMatrices{m_camera.getView(), m_camera.getProjection()};
    }
    m_renderer->render(*view, nullptr, m_fbWidth, m_fbHeight);
}

void App::onResize(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    m_fbWidth = width;
    m_fbHeight = height;
}

void App::handleEvent(const SDL_Event& event)
{
    if (m_overlay)
        m_overlay->processEvent(event);
    const bool clickOnUi =
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && m_overlay && !m_overlay->editorCameraInput();
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
        GpuProfiler& profiler = m_renderer->profiler();
        profiler.beginFrame();

        SDL_Event event;
        while (SDL_PollEvent(&event))
            handleEvent(event);

        if (m_overlay)
            m_overlay->update(overlayContext());

        // With the editor, WASD only flies while looking (Unity style); otherwise W/E/R pick gizmos
        m_controller.setKeyboardEnabled(!m_overlay || m_controller.isLooking());
        m_scene.registry().ctx().get<Input>().update(!m_overlay || m_overlay->gameInput());
        m_controller.update(dt, m_camera);
        m_renderer->pollShaderChanges();

        if (m_game && m_playMode == PlayMode::Play)
            m_game->update(gameContext(), dt);

        m_scene.updateWorldTransforms();
        {
            GpuScope scope(profiler, "Sync instances");
            syncRenderInstances();
            syncRenderLights();
        }
        if (m_overlay && m_overlay->visible()) {
            m_overlay->renderViews(overlayContext());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, m_fbWidth, m_fbHeight);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            GpuScope scope(profiler, "Game (fullscreen)");
            renderToWindow();
        }
        if (m_overlay) {
            GpuScope scope(profiler, "UI");
            m_overlay->render();
        }
        profiler.endFrame();
        SDL_GL_SwapWindow(m_window);
    }
}
