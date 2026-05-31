cmake_minimum_required(VERSION 3.20)
project(rev_gltf)

# ---------------------------------------------------------------------------
# Download cgltf.h on first configure if not already present
# cgltf: single-header C99 glTF 2.0 parser — no STL, no exceptions, ~300 KB
# https://github.com/jkuhlmann/cgltf  (MIT licence)
# ---------------------------------------------------------------------------
set(CGLTF_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(CGLTF_HEADER "${CGLTF_DIR}/cgltf.h")

if(NOT EXISTS "${CGLTF_HEADER}")
    message(STATUS "rev_gltf: downloading cgltf.h...")
    file(MAKE_DIRECTORY "${CGLTF_DIR}")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/jkuhlmann/cgltf/master/cgltf.h"
        "${CGLTF_HEADER}"
        SHOW_PROGRESS
        STATUS _dl_status
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        message(WARNING
            "rev_gltf: failed to download cgltf.h (${_dl_status}).\n"
            "  Please manually copy cgltf.h from "
            "https://github.com/jkuhlmann/cgltf to ${CGLTF_DIR}")
    else()
        message(STATUS "rev_gltf: cgltf.h downloaded OK")
    endif()
endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(rev_gltf STATIC
    src/rev_gltf.cpp
)

target_include_directories(rev_gltf PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(rev_gltf PRIVATE
    ${CGLTF_DIR}
)

target_link_libraries(rev_gltf PUBLIC
    rev_mesh
)

if(MSVC)
    # cgltf uses C99-style declarations — suppress MSVC pedantic warnings
    target_compile_options(rev_gltf PRIVATE /W3 /wd4996)
else()
    target_compile_options(rev_gltf PRIVATE -Wall -Wextra -Wno-unused-function)
endif()
