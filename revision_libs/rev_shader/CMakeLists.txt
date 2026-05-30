cmake_minimum_required(VERSION 3.20)
project(rev_shader)

add_library(rev_shader STATIC
    src/shader.cpp
)

target_include_directories(rev_shader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rev_shader PRIVATE
    opengl32
)

# Size optimization
if(MSVC)
    target_compile_options(rev_shader PRIVATE /W4)
else()
    target_compile_options(rev_shader PRIVATE -Wall -Wextra)
endif()
