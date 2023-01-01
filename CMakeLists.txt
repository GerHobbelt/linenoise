cmake_minimum_required(VERSION 3.11.0)
project(linenoise)

include_directories(include)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_executable(lnExample src/linenoise.c src/example.c)
add_library(linenoise src/linenoise.c)

