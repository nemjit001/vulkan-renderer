
set(VENDORED_BASE_DIR "${CMAKE_SOURCE_DIR}/vendored/")

set(SDL_TESTS OFF CACHE BOOL "" FORCE)

find_package(Vulkan REQUIRED)
add_subdirectory("${VENDORED_BASE_DIR}/SDL/")
add_subdirectory("${VENDORED_BASE_DIR}/volk/")
