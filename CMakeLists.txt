cmake_minimum_required(VERSION 3.31)
project(VV)

set(CMAKE_CXX_STANDARD 20)

add_executable(VV main.cpp
        Renderer.cpp
        Renderer.h)

add_subdirectory(lib)
target_link_libraries(VV PRIVATE SDL3::SDL3)

# Add Vulkan
set(VULKAN_SDK $ENV{VULKAN_SDK})
set(CMAKE_PREFIX_PATH "${VULKAN_SDK}")

find_package(Vulkan REQUIRED)
target_link_libraries(VV PRIVATE Vulkan::Vulkan)

# Copy the SDL3 DLL
add_custom_command(TARGET VV POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:VV>
)