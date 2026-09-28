cmake_minimum_required(VERSION 3.20)
project(rev_audio_file)

add_library(rev_audio_file STATIC src/audio_file_player.cpp)
target_include_directories(rev_audio_file PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(rev_audio_file PUBLIC rev_runtime)
target_link_libraries(rev_audio_file PRIVATE mfplat mfreadwrite mfuuid ole32 shlwapi)

if(MSVC)
    target_compile_options(rev_audio_file PRIVATE /W4)
else()
    target_compile_options(rev_audio_file PRIVATE -Wall -Wextra)
endif()
