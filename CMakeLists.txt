cmake_minimum_required(VERSION 2.8)

Project(linenoise)

set(SRC linenoise.c)
add_library(linenoise ${SRC})
set_target_properties(linenoise PROPERTIES DEBUG_POSTFIX "d")

set(Example_SRC example.c)
add_executable(example ${Example_SRC})
set_target_properties(example PROPERTIES DEBUG_POSTFIX "d")
target_link_libraries(example linenoise)

# Install instructions for this target
install(TARGETS linenoise
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
install(FILES linenoise.h DESTINATION include COMPONENT dev)