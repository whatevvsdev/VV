cmake_minimum_required(VERSION 3.31)
project(CompileShaders)

add_custom_target(CompileShaders ALL
        COMMAND cmd /c "${CMAKE_CURRENT_SOURCE_DIR}/../compile_shaders.bat"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../"
        COMMENT "Compiling shaders."
)

# Copy shaders after build
set(SHADER_TARGET_DIR "${CMAKE_BINARY_DIR}/shaders")
set(COMPILED_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/spirv-out")
add_custom_command(TARGET CompileShaders POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_TARGET_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${COMPILED_SHADER_DIR}"
        "${SHADER_TARGET_DIR}"
        COMMENT "Copying compiled shaders to ${SHADER_TARGET_DIR}"
)