cmake_minimum_required(VERSION 3.20)
project(FolderIconC LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Windows subsystem (no console window)
set(CMAKE_WIN32_EXECUTABLE ON)

add_executable(FolderIconC WIN32 main.c)

target_compile_definitions(FolderIconC PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

# Link Windows libraries
target_link_libraries(FolderIconC PRIVATE
    user32
    shell32
    gdi32
    comctl32
    dwmapi
    uxtheme
    ole32
)

# Optimization flags for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(FolderIconC PRIVATE /O2 /GL)
    target_link_options(FolderIconC PRIVATE /LTCG)
endif()

# Set output name
set_target_properties(FolderIconC PROPERTIES
    OUTPUT_NAME "FolderIcon"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
