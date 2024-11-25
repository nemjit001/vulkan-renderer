cmake_minimum_required(VERSION 3.14)
project("Vulkan Renderer" VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET "hidden")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")

include("utils.cmake")
include("dependencies.cmake")

file(GLOB_RECURSE VK_RENDERER_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")
file(GLOB_RECURSE VK_RENDERER_SHADER_SOURCES CONFIGURE_DEPENDS "src/*.glsl")
file(GLOB_RECURSE VK_RENDERER_SHADER_HEADERS CONFIGURE_DEPENDS "src/*.glsl.h")
add_executable(VKRenderer ${VK_RENDERER_SOURCES})
target_include_directories(VKRenderer PRIVATE "src/")
target_link_libraries(VKRenderer PRIVATE assimp::assimp glm::glm-header-only SDL2::SDL2 tinyobjloader volk::volk_headers Vulkan::Headers vendored::imgui vendored::stb)
target_enable_warnings_as_errors(VKRenderer)
target_compile_shaders(VKRenderer "${VK_RENDERER_SHADER_SOURCES}" "${VK_RENDERER_SHADER_HEADERS}")
target_copy_data_folder(VKRenderer)
