cmake_minimum_required(VERSION 3.20)
project(demo_intro)

add_executable(demo_intro
    main.cpp
)

target_link_libraries(demo_intro PRIVATE
    rev_platform
    rev_shader
    rev_curve
    rev_sequence
)

# Windows subsystem for release builds (no console)
if(MSVC)
    set_target_properties(demo_intro PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS"
    )
endif()
