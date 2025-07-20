cmake_minimum_required(VERSION 3.31)
project(DearImGUI)

set(CMAKE_CXX_STANDARD 20)
add_library(DearImGUI STATIC
        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp)

target_compile_definitions(DearImGUI PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_include_directories(DearImGUI PUBLIC imgui imgui/backends)
