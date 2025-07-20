cmake_minimum_required(VERSION 3.31)
project(VV)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")

add_executable(VV main.cpp
        Renderer.cpp
        Renderer.h
        types.h
        io.cpp
        io.h
        vk_renderer_core.cpp
        renderer_core.h
        deletion_queue.h
)

add_subdirectory(lib)

add_subdirectory(shaders)
add_dependencies(VV CompileShaders)

# Add Vulkan
set(VULKAN_SDK $ENV{VULKAN_SDK})
set(CMAKE_PREFIX_PATH "${VULKAN_SDK}")

find_package(Vulkan REQUIRED)
target_link_libraries(VV PRIVATE Vulkan::Vulkan)
target_link_libraries(VV PRIVATE SDL3::SDL3)
target_link_libraries(VV PRIVATE VulkanMemoryAllocator)