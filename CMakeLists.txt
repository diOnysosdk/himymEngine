cmake_minimum_required(VERSION 3.20)
project(HiMYM VERSION 1.0.0 LANGUAGES CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build configuration
if(MSVC)
    # Size optimization flags for release
    set(CMAKE_CXX_FLAGS_RELEASE "/O1 /GS- /GL /MT")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-Os -fno-exceptions -fno-rtti")
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Add library subdirectories
add_subdirectory(revision_libs/rev_platform)
add_subdirectory(revision_libs/rev_runtime)
add_subdirectory(revision_libs/rev_shader)
add_subdirectory(revision_libs/rev_xm)
add_subdirectory(revision_libs/rev_curve)
add_subdirectory(revision_libs/rev_sequence)
add_subdirectory(revision_libs/rev_pack)
add_subdirectory(revision_libs/rev_editor)
add_subdirectory(revision_libs/rev_mesh)
add_subdirectory(revision_libs/rev_gltf)

# Add examples
add_subdirectory(examples/minimal_intro)
add_subdirectory(examples/animated_intro)
add_subdirectory(examples/editor_app)
add_subdirectory(examples/export_test)
add_subdirectory(examples/demo_intro)
add_subdirectory(examples/mesh_demo)
add_subdirectory(examples/simple_test)
add_subdirectory(examples/indexed_test)
