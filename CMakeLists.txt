cmake_minimum_required(VERSION 3.11.0)
project(linenoise)

include_directories(include)

add_executable(lnExample linenoise.c utf8.c example.c)
add_library(linenoise linenoise.c utf8.c)

