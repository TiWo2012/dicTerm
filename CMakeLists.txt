cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_COMPILER clang CACHE STRING "C compiler" FORCE)

project(dicTerm LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c2y -Wall -Wextra -Werror -g")

find_package(raylib REQUIRED)

# Main executable – exclude test files.
file(GLOB_RECURSE ALL_C_SOURCES CONFIGURE_DEPENDS src/*.c)
list(FILTER ALL_C_SOURCES EXCLUDE REGEX ".*test.*")
add_executable(${PROJECT_NAME} ${ALL_C_SOURCES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        raylib
)

# Scrollback module (used by the main executable via src/main.c).
# Already compiled into the main executable via the GLOB_RECURSE above.

# Standalone test executables (no raylib dependency).
add_executable(test_parser
    src/parser.c
    src/test_parser.c
)
target_include_directories(test_parser
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_scrollback
    src/scrollback.c
    src/test_scrollback.c
)
target_include_directories(test_scrollback
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Register with CTest.
enable_testing()
add_test(NAME test_parser COMMAND test_parser)
add_test(NAME test_scrollback COMMAND test_scrollback)

# Auto-run tests in Debug builds: tests must pass before the build completes.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(TARGET test_parser POST_BUILD
        COMMAND $<TARGET_FILE:test_parser>
        COMMENT "Running parser unit tests..."
        USES_TERMINAL
    )
    add_custom_command(TARGET test_scrollback POST_BUILD
        COMMAND $<TARGET_FILE:test_scrollback>
        COMMENT "Running scrollback unit tests..."
        USES_TERMINAL
    )
    # Ensure the main target cannot succeed unless tests pass.
    add_dependencies(${PROJECT_NAME} test_parser test_scrollback)
endif()
