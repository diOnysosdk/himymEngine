cmake_minimum_required(VERSION 3.20)
project(rev_pack)

add_library(rev_pack STATIC src/rev_pack.cpp)
target_include_directories(rev_pack PUBLIC include)
target_link_libraries(rev_pack PRIVATE rev_shader)
