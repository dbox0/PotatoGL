cmake_minimum_required(VERSION 3.24)
project(renderer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug)
endif()

include(FetchContent)

option(BUILD_EDITOR "Build the ImGui editor overlay" ON)

# SDL3 and glm use an installed package if found, otherwise they are downloaded.
# A downloaded SDL3 is built static so no DLL has to sit next to the executable.
set(SDL_SHARED OFF CACHE BOOL "")
set(SDL_STATIC ON CACHE BOOL "")
set(SDL_TEST_LIBRARY OFF CACHE BOOL "")
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.20
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS CONFIG)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS)

# SOURCE_SUBDIR points at a missing dir so their own CMakeLists are skipped
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.15
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR none)
FetchContent_Declare(ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG v0.23.1
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR none)
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/6b5799bae8a1fe3792691ef4b72ae26434c3e799.tar.gz
    SOURCE_SUBDIR none)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.93.0-docking
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR none)
FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 5b473bc30438dc128520b90b688745bca85c4a91
    SOURCE_SUBDIR none)

FetchContent_Declare(EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v4.0.0
    GIT_SHALLOW TRUE)

FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)

FetchContent_MakeAvailable(SDL3 glm cgltf ufbx stb EnTT json)

# LTO for optimized builds, so script hooks inline into the runner's loops across files.
# Set after the dependencies so only targets defined below get it.
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR LANGUAGES C CXX)
if(IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
endif()

# glad 2.0.8, GL 4.3 core, no extensions. Regenerate with:
#   python -m glad --api "gl:core=4.3" --extensions "" --reproducible --out-path src/third_party/glad c
add_library(glad STATIC src/third_party/glad/src/gl.c)
target_include_directories(glad SYSTEM PUBLIC src/third_party/glad/include)

add_library(cgltf STATIC src/third_party/cgltf_impl.c)
target_include_directories(cgltf SYSTEM PUBLIC ${cgltf_SOURCE_DIR})

add_library(ufbx STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(ufbx SYSTEM PUBLIC ${ufbx_SOURCE_DIR})

add_library(stb_image STATIC src/third_party/stb_image_impl.c)
target_include_directories(stb_image SYSTEM PUBLIC ${stb_SOURCE_DIR})


set(WARNING_FLAGS "$<IF:$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>,/W4;/permissive-,-Wall;-Wextra;-Wpedantic>")

# Everything except the editor; must never link imgui
add_library(engine STATIC
        src/engine/anim/AnimAsset.cpp
        src/engine/anim/AnimAsset.h
        src/engine/anim/AnimationSystem.cpp
        src/engine/anim/AnimationSystem.h
        src/engine/anim/Clip.h
        src/engine/anim/Ik.cpp
        src/engine/anim/Ik.h
        src/engine/anim/Pose.cpp
        src/engine/anim/Pose.h
        src/engine/anim/Skeleton.h
        src/engine/app/App.cpp
        src/engine/app/App.h
        src/engine/app/CameraController.cpp
        src/engine/app/CameraController.h
        src/engine/app/Game.h
        src/engine/app/Overlay.h
        src/engine/app/PlayMode.h
        src/engine/assets/AssetCache.cpp
        src/engine/assets/AssetCache.h
        src/engine/assets/AssetTypes.cpp
        src/engine/assets/AssetTypes.h
        src/engine/assets/FbxLoader.cpp
        src/engine/assets/FbxLoader.h
        src/engine/assets/GltfLoader.cpp
        src/engine/assets/GltfLoader.h
        src/engine/assets/Image.cpp
        src/engine/assets/Image.h
        src/engine/assets/Material.cpp
        src/engine/assets/Material.h
        src/engine/assets/Model.cpp
        src/engine/assets/Model.h
        src/engine/assets/Primitives.cpp
        src/engine/assets/Primitives.h
        src/engine/assets/SpriteMeta.cpp
        src/engine/assets/SpriteMeta.h
        src/engine/core/InplaceFunction.h
        src/engine/core/EventBus.h
        src/engine/core/Input.h
        src/engine/core/LogCapture.cpp
        src/engine/core/LogCapture.h
        src/engine/core/Paths.cpp
        src/engine/core/Paths.h
        src/engine/reflect/ComponentRegistry.h
        src/engine/gfx/DrawCommand.h
        src/engine/gfx/DrawData.h
        src/engine/gfx/GeometryBuffer.cpp
        src/engine/gfx/GeometryBuffer.h
        src/engine/gfx/GpuProfiler.cpp
        src/engine/gfx/GpuProfiler.h
        src/engine/gfx/IndirectBuffer.cpp
        src/engine/gfx/IndirectBuffer.h
        src/engine/gfx/LightData.h
        src/engine/gfx/MaterialData.h
        src/engine/gfx/OutlineMask.cpp
        src/engine/gfx/OutlineMask.h
        src/engine/gfx/PickBuffer.cpp
        src/engine/gfx/PickBuffer.h
        src/engine/gfx/RenderTarget.cpp
        src/engine/gfx/RenderTarget.h
        src/engine/gfx/Shader.cpp
        src/engine/gfx/Shader.h
        src/engine/gfx/ShadowMap.cpp
        src/engine/gfx/ShadowMap.h
        src/engine/gfx/RangeAllocator.h
        src/engine/gfx/SkinnedGeometry.cpp
        src/engine/gfx/SkinnedGeometry.h
        src/engine/gfx/TextureArray.cpp
        src/engine/gfx/TextureArray.h
        src/engine/gfx/Vertex.h
        src/engine/render/LightClusters.cpp
        src/engine/render/LightClusters.h
        src/engine/render/Renderer.cpp
        src/engine/render/Renderer.h
        src/engine/render/UIRenderer.cpp
        src/engine/render/UIRenderer.h
        src/engine/scene/Camera.cpp
        src/engine/scene/Camera.h
        src/engine/scene/ComponentReflection.cpp
        src/engine/scene/ComponentReflection.h
        src/engine/scene/Components.h
        src/engine/scene/Prefab.cpp
        src/engine/scene/Prefab.h
        src/engine/scene/Scene.cpp
        src/engine/scene/Scene.h
        src/engine/scene/SceneSerializer.cpp
        src/engine/scene/SceneSerializer.h
        src/engine/scene/Transform.cpp
        src/engine/scene/Transform.h
        src/engine/script/ScriptContext.h
        src/engine/script/ScriptRegistry.h
        src/engine/script/ScriptRunner.cpp
        src/engine/script/EventListeners.h
        src/engine/script/ScriptRunner.h
        src/engine/ui/UiComponents.h
        src/engine/ui/UiHitTest.cpp
        src/engine/ui/UiHitTest.h
        src/engine/ui/UiLayout.cpp
        src/engine/ui/UiLayout.h
        src/engine/ui/UiReflection.cpp
        src/engine/ui/UiReflection.h)
target_include_directories(engine PUBLIC src)
target_link_libraries(engine PUBLIC glad EnTT::EnTT SDL3::SDL3 glm::glm PRIVATE cgltf ufbx stb_image nlohmann_json::nlohmann_json)
# Debug and editor builds read the source tree, so shader edits hot reload and the editor's
# Project view edits the real assets, not the copy next to the executable
target_compile_definitions(engine PRIVATE "$<$<OR:$<CONFIG:Debug>,$<BOOL:${BUILD_EDITOR}>>:DEV_RESOURCE_DIR=\"${CMAKE_SOURCE_DIR}/\">")
target_compile_options(engine PRIVATE ${WARNING_FLAGS})

if(BUILD_EDITOR)
    FetchContent_MakeAvailable(imgui imguizmo)
    add_library(imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_demo.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
            ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
    target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imguizmo_SOURCE_DIR}/src)
    target_link_libraries(imgui PUBLIC SDL3::SDL3)

    add_library(editor STATIC
            src/editor/AnimationViewer.cpp
            src/editor/AnimationViewer.h
            src/editor/AssetOps.cpp
            src/editor/AssetOps.h
            src/editor/CanvasPanel.cpp
            src/editor/CanvasPanel.h
            src/editor/ConsolePanel.cpp
            src/editor/ConsolePanel.h
            src/editor/DebugPanel.cpp
            src/editor/DebugPanel.h
            src/editor/DragDrop.h
            src/editor/EditCommands.cpp
            src/editor/EditCommands.h
            src/editor/EditHistory.cpp
            src/editor/EditHistory.h
            src/editor/Editor.cpp
            src/editor/Editor.h
            src/editor/InspectorPanel.cpp
            src/editor/InspectorPanel.h
            src/editor/ProfilerPanel.cpp
            src/editor/ProfilerPanel.h
            src/editor/ProjectPanel.cpp
            src/editor/ProjectPanel.h
            src/editor/SceneActions.cpp
            src/editor/SceneActions.h
            src/editor/SceneDialogs.cpp
            src/editor/SceneDialogs.h
            src/editor/SceneGizmos.cpp
            src/editor/SceneGizmos.h
            src/editor/SceneHierarchyPanel.cpp
            src/editor/SceneHierarchyPanel.h
            src/editor/SceneViewPanel.cpp
            src/editor/SceneViewPanel.h
            src/editor/Selection.h
            src/editor/ThumbnailCache.cpp
            src/editor/ThumbnailCache.h
            src/editor/ViewportPanel.cpp
            src/editor/ViewportPanel.h)
    target_link_libraries(editor PUBLIC engine PRIVATE imgui nlohmann_json::nlohmann_json)
    target_compile_options(editor PRIVATE ${WARNING_FLAGS})
endif()

# Gameplay; links engine, never imgui. OBJECT so REGISTER_SCRIPT registrars aren't
# dropped by the linker; new files are picked up on the next build.
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp src/game/*.h)
add_library(game OBJECT ${GAME_SOURCES})
target_link_libraries(game PUBLIC engine)
target_compile_options(game PRIVATE ${WARNING_FLAGS})

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE engine game)
if(BUILD_EDITOR)
    target_link_libraries(${PROJECT_NAME} PRIVATE editor)
    target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_EDITOR)
endif()
target_compile_options(${PROJECT_NAME} PRIVATE ${WARNING_FLAGS})

# shaders/ and assets/ are copied next to the executable on every build and loaded
# from there, except in Debug and editor builds, which read the source tree (DEV_RESOURCE_DIR above)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
    set(COPY_DIR_CMD copy_directory_if_different)
else()
    set(COPY_DIR_CMD copy_directory)
endif()
add_custom_target(copy_resources ALL
        COMMAND ${CMAKE_COMMAND} -E ${COPY_DIR_CMD} ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
        COMMAND ${CMAKE_COMMAND} -E ${COPY_DIR_CMD} ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
        COMMENT "Copying shaders and assets")
add_dependencies(${PROJECT_NAME} copy_resources)
