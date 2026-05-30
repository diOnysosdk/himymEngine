cmake_minimum_required(VERSION 3.20)
project(rev_xm)

# libxm-windows integration (C89-compatible fork for MSVC)
# https://github.com/MikeEviscerate/libxm-windows
message(STATUS "Building rev_xm with libxm-windows (C89-compatible)")
add_subdirectory(third_party/libxm/src)
add_library(rev_xm STATIC src/xm_player.cpp)
target_link_libraries(rev_xm PRIVATE xm)
target_compile_definitions(rev_xm PRIVATE REV_XM_ENABLED=1)
target_include_directories(rev_xm PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/libxm/src)

target_include_directories(rev_xm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Size optimization
if(MSVC)
    target_compile_options(rev_xm PRIVATE /W4)
else()
    target_compile_options(rev_xm PRIVATE -Wall -Wextra)
endif()
