cmake_minimum_required(VERSION 3.21)
include(CheckCCompilerFlag)

project(libxmize LANGUAGES C)

foreach(std 23 17 11 99)
    if(MSVC)
        check_c_compiler_flag("/std:c${std}" C_STD_${std}_SUPPORTED)
    else()
        check_c_compiler_flag("-std=c${std}" C_STD_${std}_SUPPORTED)
    endif()
    if(C_STD_${std}_SUPPORTED)
        set(CMAKE_C_STANDARD ${std})
        break()
    endif()
endforeach()

if(NOT CMAKE_C_STANDARD)
    message(FATAL_ERROR "C99 or newer is required for building via CMake. Use the VS 6.0 or VS 2005 projects for C89")
endif()

set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)


option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)

add_subdirectory(../../src xm_build)

add_executable(libxmize libxmize.c)
target_link_libraries(libxmize PRIVATE xm)

