cmake_minimum_required(VERSION 3.20)
project(rev_xm)

# libxm integration - requires C23 with stdbit.h (not yet widely available)
# Tested: GCC 15.2.0 ❌ | Clang 18.1.8 ❌ | MSVC ❌ (no C23 at all)
# Stub implementation until compilers implement C23 stdbit.h
message(STATUS "Building rev_xm stub (libxm requires C23 stdbit.h - not available in any compiler yet)")
add_library(rev_xm STATIC src/xm_player.cpp)

target_include_directories(rev_xm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Size optimization
if(MSVC)
    target_compile_options(rev_xm PRIVATE /W4)
else()
    target_compile_options(rev_xm PRIVATE -Wall -Wextra)
endif()
