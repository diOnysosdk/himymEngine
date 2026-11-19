cmake_minimum_required(VERSION 3.20)
project(rev_xm)

# libxm-windows integration (C89-compatible fork for MSVC)
# https://github.com/MikeEviscerate/libxm-windows
message(STATUS "Building rev_xm with libxm-windows (C89-compatible)")
if(HIMYM_CRINKLER_EXECUTABLE)
    set(XM_VERBOSE OFF CACHE BOOL "" FORCE)
    set(XM_STRINGS OFF CACHE BOOL "" FORCE)
    set(XM_TIMING_FUNCTIONS OFF CACHE BOOL "" FORCE)
    set(XM_MUTING_FUNCTIONS OFF CACHE BOOL "" FORCE)
    set(XM_SAMPLE_RATE 48000 CACHE STRING "" FORCE)
    if(DEFINED HIMYM_PACKED_XM_DISABLED_EFFECTS)
        set(XM_DISABLED_EFFECTS "${HIMYM_PACKED_XM_DISABLED_EFFECTS}" CACHE STRING "" FORCE)
        set(XM_DISABLED_VOLUME_EFFECTS "${HIMYM_PACKED_XM_DISABLED_VOLUME_EFFECTS}" CACHE STRING "" FORCE)
        set(XM_DISABLED_FEATURES "${HIMYM_PACKED_XM_DISABLED_FEATURES}" CACHE STRING "" FORCE)
        set(XM_PANNING_TYPE "${HIMYM_PACKED_XM_PANNING_TYPE}" CACHE STRING "" FORCE)
        message(STATUS "Using project-specific packed XM feature mask")
    endif()
endif()
add_subdirectory(third_party/libxm/src)
# libxm enables IPO in its own CMake project. Crinkler consumes ordinary COFF
# objects and cannot resolve the resulting MSVC LTCG archive members.
if(HIMYM_CRINKLER_EXECUTABLE)
    set_property(TARGET xm PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
endif()
add_library(rev_xm STATIC src/xm_player.cpp)
if(HIMYM_CRINKLER_EXECUTABLE)
    target_compile_definitions(rev_xm PUBLIC HIMYM_CRINKLER_BUILD)
endif()
target_link_libraries(rev_xm PRIVATE xm)
target_compile_definitions(rev_xm PRIVATE REV_XM_ENABLED=1)
target_include_directories(rev_xm PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/libxm/src)

target_include_directories(rev_xm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/revision_libs/rev_runtime/include
    ${CMAKE_SOURCE_DIR}/revision_libs/rev_curve/include
)

# Size optimization
if(MSVC)
    target_compile_options(rev_xm PRIVATE /W4)
else()
    target_compile_options(rev_xm PRIVATE -Wall -Wextra)
endif()
