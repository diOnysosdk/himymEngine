cmake_minimum_required(VERSION 3.20)
project(rev_particles)

add_library(rev_particles STATIC
    src/rev_particles.cpp
)

target_include_directories(rev_particles PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(rev_particles PRIVATE /W4)
else()
    target_compile_options(rev_particles PRIVATE -Wall -Wextra)
endif()
