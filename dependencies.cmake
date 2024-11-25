
set(VENDORED_BASE_DIR "${CMAKE_SOURCE_DIR}/vendored/")

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)

find_package(Vulkan REQUIRED)
add_subdirectory("${VENDORED_BASE_DIR}/assimp/")
add_subdirectory("${VENDORED_BASE_DIR}/glm/")
add_subdirectory("${VENDORED_BASE_DIR}/SDL/")
add_subdirectory("${VENDORED_BASE_DIR}/tinyobjloader/")
add_subdirectory("${VENDORED_BASE_DIR}/volk/")

add_library(imgui STATIC "${VENDORED_BASE_DIR}/imgui/imgui.cpp" "${VENDORED_BASE_DIR}/imgui/imgui_demo.cpp" "${VENDORED_BASE_DIR}/imgui/imgui_draw.cpp"
	"${VENDORED_BASE_DIR}/imgui/imgui_tables.cpp" "${VENDORED_BASE_DIR}/imgui/imgui_widgets.cpp"
	"${VENDORED_BASE_DIR}/imgui/backends/imgui_impl_sdl2.cpp"   # SDL2 hook
	"${VENDORED_BASE_DIR}/imgui/backends/imgui_impl_vulkan.cpp" # Vulkan hook
)
target_include_directories(imgui PUBLIC "vendored/imgui/" "vendored/imgui/backends/")
target_link_libraries(imgui PUBLIC SDL2::SDL2 Vulkan::Headers)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES) # Definitions required by imgui to use vulkan loader
add_library(vendored::imgui ALIAS imgui)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "${VENDORED_BASE_DIR}/stb/")
add_library(vendored::stb ALIAS stb)
