add_executable(simple_test
    main.cpp
)

target_link_libraries(simple_test PRIVATE
    rev_platform
    rev_shader
    opengl32
    gdi32
    user32
)

# Size optimization
if(MSVC)
    target_compile_options(simple_test PRIVATE /O1 /GS-)
    target_link_options(simple_test PRIVATE /SUBSYSTEM:WINDOWS)
endif()
