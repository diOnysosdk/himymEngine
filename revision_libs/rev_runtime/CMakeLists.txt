cmake_minimum_required(VERSION 3.20)
project(rev_runtime)

add_library(rev_runtime STATIC
    src/rev_runtime.cpp
)

target_include_directories(rev_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../rev_curve/include
)

target_link_libraries(rev_runtime PUBLIC
    opengl32
    gdiplus
    rev_curve
)
