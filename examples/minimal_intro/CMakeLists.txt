cmake_minimum_required(VERSION 3.20)
project(minimal_intro)

add_executable(minimal_intro
    main.cpp
)

target_link_libraries(minimal_intro PRIVATE
    rev_platform
    rev_shader
    rev_xm
)

# Windows subsystem for release builds
if(MSVC)
    set_target_properties(minimal_intro PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS"
    )
endif()
