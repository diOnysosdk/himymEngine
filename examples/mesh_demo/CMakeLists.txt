cmake_minimum_required(VERSION 3.20)
project(mesh_demo)

add_executable(mesh_demo main.cpp)

target_link_libraries(mesh_demo PRIVATE
    rev_platform
    rev_shader
    rev_mesh
    opengl32
    gdi32
    user32
)

# Size optimization
if(MSVC)
    target_compile_options(mesh_demo PRIVATE
        $<$<CONFIG:Release>:/O1>
        $<$<CONFIG:Release>:/GS->
    )
    target_link_options(mesh_demo PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()
