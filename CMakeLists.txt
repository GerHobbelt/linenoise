cmake_minimum_required(VERSION 3.11.0)

cmake_policy(SET CMP0076 NEW)

project(linenoise)

add_library(Linenoise)

target_include_directories(Linenoise INTERFACE
                           ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(Linenoise PUBLIC linenoise.h utf8.h)

target_sources(Linenoise PRIVATE linenoise.c utf8.c)


add_executable(lnExample example.c)
target_link_libraries(lnExample Linenoise)
