cmake_minimum_required(VERSION 3.10)
project(linenoise-win32 VERSION 1.0.0 LANGUAGES C)

# This project can use C11, but will gracefully decay down to C89.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_C_FLAGS_RELEASE "/MT")
set(CMAKE_C_FLAGS_DEBUG "/MTd")

add_definitions(/W4)
add_definitions(-DWIN32_LEAN_AND_MEAN)
add_definitions(-DUNICODE -D_UNICODE)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

include_directories(
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}"
)

add_library(linenoise STATIC
    "${PROJECT_SOURCE_DIR}/ht.c"
    "${PROJECT_SOURCE_DIR}/linenoise.c"
)

add_executable(example
    "${PROJECT_SOURCE_DIR}/example.c"
    "${PROJECT_SOURCE_DIR}/ht.c"
    "${PROJECT_SOURCE_DIR}/linenoise.c"
)
