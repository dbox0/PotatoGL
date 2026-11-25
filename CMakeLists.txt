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
glad_add_library(glad_gl_core_46 REPRODUCIBLE API gl:core=4.6)

add_executable(renderer src/main.cpp)
target_link_libraries(renderer PRIVATE glad_gl_core_46 SDL3::SDL3 glm::glm OpenGL::GL)
target_compile_definitions(renderer PRIVATE SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders/")
target_compile_options(renderer PRIVATE -Wall -Wextra -Wpedantic)
