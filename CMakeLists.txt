cmake_minimum_required(VERSION 3.24)
project(opengl_renderer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(OpenGL REQUIRED)
find_package(SDL3 REQUIRED CONFIG)
find_package(glm REQUIRED)

# glad2 loader, generated at configure time (needs python3 + jinja2)
include(FetchContent)
FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake)
FetchContent_MakeAvailable(glad)
# GL 4.3 core: Intel HD 4600 (Haswell) on Windows tops out at 4.3; needed for multi-draw indirect
glad_add_library(glad_gl_core_43 REPRODUCIBLE API gl:core=4.3)

# Model loaders. SOURCE_SUBDIR points at a missing dir so their own
# CMakeLists (tests etc.) are skipped; we build the sources ourselves.
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
FetchContent_MakeAvailable(cgltf ufbx)

add_library(cgltf STATIC src/third_party/cgltf_impl.c)
target_include_directories(cgltf SYSTEM PUBLIC ${cgltf_SOURCE_DIR})

add_library(ufbx STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(ufbx SYSTEM PUBLIC ${ufbx_SOURCE_DIR})

add_executable(renderer src/main.cpp
        src/scene/Camera.cpp
        src/scene/Camera.h
        src/scene/Transform.cpp
        src/scene/Transform.h
        src/gfx/Vertex.h
        src/gfx/DrawCommand.h
        src/gfx/GeometryBuffer.cpp
        src/gfx/GeometryBuffer.h
        src/gfx/IndirectBuffer.cpp
        src/gfx/IndirectBuffer.h
        src/assets/GltfLoader.cpp
        src/assets/GltfLoader.h)
target_link_libraries(renderer PRIVATE glad_gl_core_43 cgltf ufbx SDL3::SDL3 glm::glm OpenGL::GL)
target_compile_definitions(renderer PRIVATE
        SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders/"
        ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/")
target_compile_options(renderer PRIVATE -Wall -Wextra -Wpedantic)
