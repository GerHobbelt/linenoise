cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
project(linenoise)
add_library(linenoise linenoise.c)
set_target_properties(linenoise PROPERTIES PUBLIC_HEADER linenoise.h)
target_include_directories(linenoise PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
	$<INSTALL_INTERFACE:include>
)
install(TARGETS linenoise)
