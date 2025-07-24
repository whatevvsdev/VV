cmake_minimum_required(VERSION 3.31)
project(VV)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")

add_executable(VV main.cpp
        engine/renderer/renderer.cpp
        engine/renderer/renderer.h
        common/types.h
        common/io.cpp
        common/io.h
        engine/renderer/vk_renderer_core.cpp
        engine/renderer/renderer_core.h
        common/function_queue.h
        engine/renderer/vv_vulkan.h
        game/game.cpp
        game/game.h
        engine/renderer/cameras.cpp
        engine/renderer/cameras.h
        engine/renderer/compute_pipeline.cpp
        engine/renderer/compute_pipeline.h
        engine/renderer/profiling.cpp
        engine/renderer/profiling.h
        engine/data/magicavoxel_parser.cpp
        engine/data/magicavoxel_parser.h
)

# Force SDL to be compiled into
set(SDL_SHARED OFF CACHE BOOL "Disable SDL3.dll" FORCE)
set(SDL_STATIC ON CACHE BOOL "Enable SDL3.lib" FORCE)

add_subdirectory(lib)

add_subdirectory(shaders)
add_dependencies(VV CompileShaders)

# Add Vulkan
set(VULKAN_SDK $ENV{VULKAN_SDK})
set(CMAKE_PREFIX_PATH "${VULKAN_SDK}")

target_link_libraries(VV PRIVATE DearImGUI)
target_link_libraries(VV PRIVATE SDL3::SDL3)
target_link_libraries(VV PRIVATE volk::volk_headers)
target_link_libraries(VV PRIVATE VulkanMemoryAllocator)

set(DEAR_IMGUI_BACKEND_SOURCE ${CMAKE_SOURCE_DIR}/lib/imgui/backends/)
target_sources(VV PRIVATE
        ${DEAR_IMGUI_BACKEND_SOURCE}imgui_impl_sdl3.cpp
        ${DEAR_IMGUI_BACKEND_SOURCE}imgui_impl_vulkan.cpp
)

set(FONT_FILENAME "monofonto rg.ttf")
configure_file(${CMAKE_SOURCE_DIR}/${FONT_FILENAME} ${CMAKE_BINARY_DIR}/${FONT_FILENAME} COPYONLY)
