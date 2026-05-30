cmake_minimum_required(VERSION 3.20)
project(rev_platform)

add_library(rev_platform STATIC
    src/platform_win32.cpp
    src/platform_timing.cpp
    src/platform_gl_loader.cpp
)

target_include_directories(rev_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rev_platform PRIVATE
    opengl32
    gdi32
    user32
)

# Size optimization
if(MSVC)
    target_compile_options(rev_platform PRIVATE /W4)
else()
    target_compile_options(rev_platform PRIVATE -Wall -Wextra)
endif()
