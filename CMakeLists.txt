cmake_minimum_required(VERSION 3.5.0)

project(linenoise "C")

set(src "linenoise.c")
set(inc ${CMAKE_CURRENT_SOURCE_DIR})

add_library(${PROJECT_NAME} ${src})
target_include_directories(${PROJECT_NAME} PUBLIC ${inc})
