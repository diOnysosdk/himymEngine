cmake_minimum_required(VERSION 3.20)
project(rev_editor)

# NOTE: ImGui Integration Required
# To enable the full editor, download Dear ImGui from:
# https://github.com/ocornut/imgui
# 
# Place the following files in third_party/imgui/:
# - imgui.h, imgui.cpp
# - imgui_draw.cpp, imgui_tables.cpp, imgui_widgets.cpp
# - imgui_internal.h
# - imstb_*.h files
# - backends/imgui_impl_win32.h/cpp
# - backends/imgui_impl_opengl3.h/cpp
#
# Then uncomment the ImGui library section below.

# ImGui library
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_impl_win32.cpp
    third_party/imgui/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
)

target_link_libraries(imgui PRIVATE
    opengl32
    user32
    gdi32
    imm32
)

# Editor library
add_library(rev_editor STATIC
    src/editor_context.cpp
)

target_include_directories(rev_editor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rev_editor PUBLIC
    rev_platform
    rev_runtime
    rev_sequence
    rev_curve
    rev_shader
    rev_pack
    imgui
)

# Size optimization
if(MSVC)
    target_compile_options(rev_editor PRIVATE /W4)
else()
    target_compile_options(rev_editor PRIVATE -Wall -Wextra)
endif()
