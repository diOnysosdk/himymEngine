add_executable(indexed_test
    main.cpp
)

target_link_libraries(indexed_test PRIVATE
    rev_platform
    rev_shader
    opengl32
    gdi32
    user32
)

if(MSVC)
    target_compile_options(indexed_test PRIVATE
        $<$<CONFIG:Release>:/O1>
        $<$<CONFIG:Release>:/GS->
    )
    target_link_options(indexed_test PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()
