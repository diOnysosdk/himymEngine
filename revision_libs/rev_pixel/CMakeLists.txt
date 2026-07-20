cmake_minimum_required(VERSION 3.20)
project(rev_pixel)

add_library(rev_pixel STATIC
    src/rev_pixel.cpp
)

target_include_directories(rev_pixel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(rev_pixel PRIVATE /W4)
else()
    target_compile_options(rev_pixel PRIVATE -Wall -Wextra)
endif()
