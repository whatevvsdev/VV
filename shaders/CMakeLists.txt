cmake_minimum_required(VERSION 3.31)
project(CompileShaders)

add_custom_target(CompileShaders ALL
        COMMAND cmd /c "${CMAKE_CURRENT_SOURCE_DIR}/../compile_shaders.bat"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../"
        COMMENT "Compiling shaders."
)