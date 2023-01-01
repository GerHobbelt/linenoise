cmake_minimum_required(VERSION 3.10)

cmake_policy(SET CMP0076 NEW)

add_library(Linenoise)

target_include_directories(Linenoise INTERFACE
                           ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(Linenoise PUBLIC linenoise.h)

target_sources(Linenoise PRIVATE linenoise.c)

