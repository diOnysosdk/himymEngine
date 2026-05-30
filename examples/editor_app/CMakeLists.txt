cmake_minimum_required(VERSION 3.20)
project(editor_app)

add_executable(editor_app
    main.cpp
)

target_link_libraries(editor_app PRIVATE
    rev_platform
    rev_editor
    rev_sequence
    rev_curve
)

# Console subsystem for editor (want printf output)
if(MSVC)
    set_target_properties(editor_app PROPERTIES
        LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
        LINK_FLAGS_RELEASE "/SUBSYSTEM:CONSOLE"
    )
endif()
