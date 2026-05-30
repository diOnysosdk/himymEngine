cmake_minimum_required(VERSION 3.20)
project(rev_xm)

# NOTE: libxm requires GCC/Clang (C23) and does not compile with MSVC
# To enable XM playback on Windows, use MinGW-w64 or Clang-cl
# For now, using stub implementation

add_library(rev_xm STATIC
    src/xm_player.cpp
)

target_include_directories(rev_xm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Size optimization
if(MSVC)
    target_compile_options(rev_xm PRIVATE /W4)
else()
    target_compile_options(rev_xm PRIVATE -Wall -Wextra)
endif()
