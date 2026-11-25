#include <glad/gl.h>
#include <SDL3/SDL.h>

#include "assets/GltfLoader.h"
#include "gfx/GeometryBuffer.h"
#include "gfx/IndirectBuffer.h"
#include "scene/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "Failed to open %s\n", path.c_str());
        std::exit(EXIT_FAILURE);
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static GLuint compileShader(GLenum type, const std::string& path) {
    std::string src = readFile(path);
    const char* cstr = src.c_str();
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &cstr, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error (%s):\n%s\n", path.c_str(), log);
        std::exit(EXIT_FAILURE);
    }
    return shader;
}

static GLuint createProgram(const std::string& vertPath, const std::string& fragPath) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertPath);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragPath);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error:\n%s\n", log);
        std::exit(EXIT_FAILURE);
    }
    return program;
}

static void GLAD_API_PTR glDebugCallback(GLenum, GLenum type, GLuint, GLenum severity,
                                     GLsizei, const GLchar* message, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    std::fprintf(stderr, "[GL %s] %s\n", type == GL_DEBUG_TYPE_ERROR ? "ERROR" : "debug", message);
}

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "Failed to init SDL: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
#ifndef NDEBUG
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

    SDL_Window* window = SDL_CreateWindow("OpenGL Renderer", 1280, 720,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        std::fprintf(stderr, "Failed to create GL context: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))) {
        std::fprintf(stderr, "Failed to load OpenGL\n");
        return EXIT_FAILURE;
    }
    std::printf("GL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

#ifndef NDEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(glDebugCallback, nullptr);
#endif

    int fbWidth, fbHeight;
    SDL_GetWindowSizeInPixels(window, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);

    {
        constexpr int gridSize = 10;
        constexpr int drawCount = gridSize * gridSize;
        constexpr float spacing = 2.5f;

        std::optional<MeshData> duck = loadGltf(ASSET_DIR "Duck.glb");
        if (!duck)
            return EXIT_FAILURE;

        GeometryBuffer geometry(1 << 16, 1 << 18, drawCount);
        IndirectBuffer indirect(drawCount);

        const MeshRange duckRange = geometry.addMesh(duck->vertices, duck->indices);
        std::vector<glm::mat4> models;
        models.reserve(drawCount);
        for (int z = 0; z < gridSize; ++z) {
            for (int x = 0; x < gridSize; ++x) {
                const glm::vec3 pos((x - (gridSize - 1) * 0.5f) * spacing, 0.0f, (z - (gridSize - 1) * 0.5f) * spacing);
                const float yaw = glm::radians(static_cast<float>((x * 37 + z * 61) % 360));
                models.push_back(glm::rotate(glm::translate(glm::mat4(1.0f), pos), yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
                indirect.add(duckRange);
            }
        }
        indirect.upload();

        // Indexed by drawId in mesh.vert
        GLuint modelSsbo;
        glGenBuffers(1, &modelSsbo);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, modelSsbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER, models.size() * sizeof(glm::mat4), models.data(), GL_STATIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, modelSsbo);

        GLuint program = createProgram(SHADER_DIR "mesh.vert", SHADER_DIR "mesh.frag");
        const GLint viewProjLoc = glGetUniformLocation(program, "uViewProj");

        Camera camera;
        camera.setAspect(static_cast<float>(fbWidth) / static_cast<float>(fbHeight));
        camera.getTransform().setPos({0.0f, 12.0f, 22.0f});
        camera.getTransform().lookAt({0.0f, 0.0f, 0.0f});

        glEnable(GL_DEPTH_TEST);

        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT)
                    running = false;
                else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
                    running = false;
                else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && event.window.data2 > 0) {
                    glViewport(0, 0, event.window.data1, event.window.data2);
                    camera.setAspect(static_cast<float>(event.window.data1) / static_cast<float>(event.window.data2));
                }
            }

            glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            const glm::mat4 viewProj = camera.getProjection() * camera.getView();
            glUseProgram(program);
            glUniformMatrix4fv(viewProjLoc, 1, GL_FALSE, glm::value_ptr(viewProj));
            geometry.bind();
            indirect.draw();

            SDL_GL_SwapWindow(window);
        }

        glDeleteProgram(program);
        glDeleteBuffers(1, &modelSsbo);
    }  // GL objects must die before the context

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
