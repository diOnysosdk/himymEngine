cmake_minimum_required(VERSION 3.20)
project(animated_intro)

add_executable(animated_intro
    main.cpp
)

target_link_libraries(animated_intro PRIVATE
    rev_platform
    rev_shader
    rev_curve
    rev_sequence
)

# Windows subsystem for release builds
if(MSVC)
    set_target_properties(animated_intro PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS"
    )
endif()
