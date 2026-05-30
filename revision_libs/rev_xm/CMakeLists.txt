cmake_minimum_required(VERSION 3.20)
project(rev_xm)

# NOTE: To enable XM playback, you need to add libxm:
# 1. Download libxm from https://github.com/Artefact2/libxm
# 2. Place files in third_party/libxm/
# 3. Uncomment the lines below:

# add_library(libxm STATIC
#     third_party/libxm/xm.c
# )
# 
# target_include_directories(libxm PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libxm
# )

add_library(rev_xm STATIC
    src/xm_player.cpp
)

target_include_directories(rev_xm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Uncomment when libxm is added:
# target_link_libraries(rev_xm PRIVATE libxm)

# Size optimization
if(MSVC)
    target_compile_options(rev_xm PRIVATE /W4)
else()
    target_compile_options(rev_xm PRIVATE -Wall -Wextra)
endif()
