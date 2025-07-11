cmake_minimum_required(VERSION 3.31)
project(VV)

set(CMAKE_CXX_STANDARD 20)

set(SDL3_INCLUDE_DIR "C:/SDL/SDL3-3.2.16/include")
set(SDL3_LIB_DIR "C:/SDL/SDL3-3.2.16/lib")
set(SDL3_DLL_DIR "C:/SDL/SDL3-3.2.16/bin")

include_directories(${SDL3_INCLUDE_DIR})
link_directories(${SDL3_LIB_DIR})

add_executable(VV main.cpp)
target_link_libraries(${PROJECT_NAME} SDL3)

add_custom_command(TARGET VV POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SDL3_DLL_DIR}/SDL3.dll
        $<TARGET_FILE_DIR:VV>
)