cmake_minimum_required(VERSION 3.20)
project(HiMYM VERSION 1.0.0 LANGUAGES CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HIMYM_RELEASE_PROFILE "GENERAL" CACHE STRING
    "Release profile: GENERAL for normal demos, INTRO for size-limited packed intros")
set_property(CACHE HIMYM_RELEASE_PROFILE PROPERTY STRINGS GENERAL INTRO)
string(TOUPPER "${HIMYM_RELEASE_PROFILE}" HIMYM_RELEASE_PROFILE)
if(NOT HIMYM_RELEASE_PROFILE STREQUAL "GENERAL" AND
   NOT HIMYM_RELEASE_PROFILE STREQUAL "INTRO")
    message(FATAL_ERROR "HIMYM_RELEASE_PROFILE must be GENERAL or INTRO")
endif()
message(STATUS "HiMYM Release profile: ${HIMYM_RELEASE_PROFILE}")

# Build configuration
if(MSVC)
    if(HIMYM_RELEASE_PROFILE STREQUAL "INTRO")
        # Visual Studio's default C++ flags contain /GR and /EHsc. Remove them
        # before selecting the size profile so cl.exe does not emit override
        # warnings for every translation unit.
        string(REPLACE "/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        set(CMAKE_CXX_FLAGS_RELEASE "/O1 /Os /GS- /GL /Gy /Gw /GR- /EHs-c- /MT")
        set(CMAKE_C_FLAGS_RELEASE "/O1 /Os /GS- /GL /Gy /Gw /MT")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "/O2 /GS /GL /Gy /Gw /MT")
        set(CMAKE_C_FLAGS_RELEASE "/O2 /GS /GL /Gy /Gw /MT")
    endif()
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
else()
    if(HIMYM_RELEASE_PROFILE STREQUAL "INTRO")
        set(CMAKE_CXX_FLAGS_RELEASE "-Os -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections")
        set(CMAKE_C_FLAGS_RELEASE "-Os -ffunction-sections -fdata-sections")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "-O2")
        set(CMAKE_C_FLAGS_RELEASE "-O2")
    endif()
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
add_subdirectory(revision_libs/rev_pixel)
add_subdirectory(revision_libs/rev_particles)

# Add examples
add_subdirectory(examples/minimal_intro)
add_subdirectory(examples/animated_intro)
add_subdirectory(examples/editor_app)
add_subdirectory(examples/export_test)
add_subdirectory(examples/demo_intro)
add_subdirectory(examples/mesh_demo)
add_subdirectory(examples/simple_test)
add_subdirectory(examples/indexed_test)

# Focused deterministic tests for the shared text animation evaluator.
add_executable(text_animation_tests tests/text_animation_tests.cpp)
target_link_libraries(text_animation_tests PRIVATE rev_runtime)
if(MSVC)
    set_target_properties(text_animation_tests PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup"
    )
endif()

# Focused Blender/glTF coordinate, normal, camera, and material-extras tests.
add_executable(gltf_import_tests tests/gltf_import_tests.cpp)
target_link_libraries(gltf_import_tests PRIVATE rev_gltf rev_runtime)
if(MSVC)
    set_target_properties(gltf_import_tests PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup"
    )
endif()

# Focused deterministic tests for pixel-emitter direction and cone sampling.
add_executable(particle_direction_tests tests/particle_direction_tests.cpp)
target_link_libraries(particle_direction_tests PRIVATE rev_particles)
if(MSVC)
    set_target_properties(particle_direction_tests PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup"
    )
endif()

# Packer contract tests for project-derived C++ and CMake feature manifests.
add_executable(packed_feature_manifest_tests tests/packed_feature_manifest_tests.cpp)
target_link_libraries(packed_feature_manifest_tests PRIVATE rev_pack)
if(MSVC)
    set_target_properties(packed_feature_manifest_tests PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup"
    )
endif()

add_executable(shader_pipeline_tests tests/shader_pipeline_tests.cpp)
target_link_libraries(shader_pipeline_tests PRIVATE rev_runtime)
if(MSVC)
    set_target_properties(shader_pipeline_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# Utility CLI: generate build/packed_assets.h without launching editor UI.
add_executable(pack_cli tools/pack_cli.cpp)
target_link_libraries(pack_cli PRIVATE rev_pack)
if(MSVC)
    set_target_properties(pack_cli PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup"
    )
endif()

# Headless editor persistence/export driver used by the full pipeline regression.
add_executable(editor_pipeline_cli tools/editor_pipeline_cli.cpp)
target_link_libraries(editor_pipeline_cli PRIVATE rev_editor gdiplus)
if(MSVC)
    set_target_properties(editor_pipeline_cli PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup"
    )
endif()
