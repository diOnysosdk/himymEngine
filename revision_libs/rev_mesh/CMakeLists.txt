cmake_minimum_required(VERSION 3.20)
project(rev_mesh)

add_library(rev_mesh STATIC
    src/mesh.cpp
)

target_include_directories(rev_mesh PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rev_mesh PRIVATE
    opengl32
)

# Size optimization
if(MSVC)
    target_compile_options(rev_mesh PRIVATE /W4)
else()
    target_compile_options(rev_mesh PRIVATE -Wall -Wextra)
endif()
