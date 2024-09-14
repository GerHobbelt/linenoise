cmake_minimum_required(VERSION 3.20)

project(linenoise)

set(CMAKE_BUILD_TYPE Debug)
add_library(linenoise STATIC ${PROJECT_SOURCE_DIR}/src/linenoise.c)
target_include_directories(linenoise PUBLIC ${PROJECT_SOURCE_DIR}/include)

option(BUILD_EXAMPLE "build example" OFF)
if (BUILD_EXAMPLE)
    add_executable(linenoise_example ${PROJECT_SOURCE_DIR}/example/example.c)
    target_link_libraries(linenoise_example PRIVATE linenoise)
    if (WIN32)
        target_link_libraries(linenoise_example PRIVATE ws2_32)
    endif()
endif()