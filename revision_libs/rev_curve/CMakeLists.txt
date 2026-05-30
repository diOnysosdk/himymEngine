cmake_minimum_required(VERSION 3.20)
project(rev_curve)

add_library(rev_curve STATIC
    src/curve.cpp
)

target_include_directories(rev_curve PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Size optimization
if(MSVC)
    target_compile_options(rev_curve PRIVATE /W4)
else()
    target_compile_options(rev_curve PRIVATE -Wall -Wextra)
endif()
