cmake_minimum_required(VERSION 3.20)
project(rev_xm)

# libxm integration - compiles with GCC/Clang, stub with MSVC
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Building libxm with GCC/Clang support")
    add_subdirectory(third_party/libxm)
    add_library(rev_xm STATIC src/xm_player.cpp)
    target_link_libraries(rev_xm PRIVATE xm)
    target_compile_definitions(rev_xm PRIVATE REV_XM_ENABLED=1)
else()
    message(STATUS "Building rev_xm stub (libxm requires GCC/Clang)")
    add_library(rev_xm STATIC src/xm_player.cpp)
endif()

target_include_directories(rev_xm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Size optimization
if(MSVC)
    target_compile_options(rev_xm PRIVATE /W4)
else()
    target_compile_options(rev_xm PRIVATE -Wall -Wextra)
endif()
