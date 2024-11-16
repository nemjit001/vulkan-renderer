
set(VENDORED_BASE_DIR "${CMAKE_SOURCE_DIR}/vendored/")

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)

find_package(Vulkan REQUIRED)
add_subdirectory("${VENDORED_BASE_DIR}/glm/")
add_subdirectory("${VENDORED_BASE_DIR}/SDL/")
add_subdirectory("${VENDORED_BASE_DIR}/tinyobjloader/")
add_subdirectory("${VENDORED_BASE_DIR}/volk/")

add_library(imgui STATIC "vendored/imgui/imgui.cpp" "vendored/imgui/imgui_demo.cpp" "vendored/imgui/imgui_draw.cpp"
	"vendored/imgui/imgui_tables.cpp" "vendored/imgui/imgui_widgets.cpp"
	"vendored/imgui/backends/imgui_impl_sdl2.cpp"   # SDL2 hook
	"vendored/imgui/backends/imgui_impl_vulkan.cpp" # Vulkan hook
)
target_include_directories(imgui PUBLIC "vendored/imgui/" "vendored/imgui/backends/")
target_link_libraries(imgui PUBLIC SDL2::SDL2 Vulkan::Headers)
add_library(vendored::imgui ALIAS imgui)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "${VENDORED_BASE_DIR}/stb/")
add_library(vendored::stb ALIAS stb)
