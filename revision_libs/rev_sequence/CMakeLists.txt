cmake_minimum_required(VERSION 3.20)
project(rev_sequence)

add_library(rev_sequence STATIC
    src/sequence.cpp
)

target_include_directories(rev_sequence PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Size optimization
if(MSVC)
    target_compile_options(rev_sequence PRIVATE /W4)
else()
    target_compile_options(rev_sequence PRIVATE -Wall -Wextra)
endif()
