cmake_minimum_required(VERSION 2.7)

project(linenoise)

add_library(linenoise STATIC
    linenoise.c
    encodings/utf8.c
)
