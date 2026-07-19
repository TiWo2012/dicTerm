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

# Scrollback test (no raylib dependency).
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

# Screen buffer tests (no raylib dependency).
add_executable(test_screen
    src/screen.c
    src/test_screen.c
)
target_include_directories(test_screen
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# ANSI colour / SGR tests (no raylib dependency – tests the parser
# integration with SGR colour codes).
add_executable(test_ansi_colors
    src/parser.c
    src/test_ansi_colors.c
)
target_include_directories(test_ansi_colors
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Mouse reporting tests (no raylib dependency – pure encoder).
add_executable(test_mouse
    src/mouse.c
    src/test_mouse.c
)
target_include_directories(test_mouse
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Register with CTest.
enable_testing()
add_test(NAME test_parser COMMAND test_parser)
add_test(NAME test_scrollback COMMAND test_scrollback)
add_test(NAME test_screen COMMAND test_screen)
add_test(NAME test_ansi_colors COMMAND test_ansi_colors)
add_test(NAME test_mouse COMMAND test_mouse)

# Auto-run tests in Debug builds.
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
    add_custom_command(TARGET test_screen POST_BUILD
        COMMAND $<TARGET_FILE:test_screen>
        COMMENT "Running screen buffer unit tests..."
        USES_TERMINAL
    )
    add_custom_command(TARGET test_ansi_colors POST_BUILD
        COMMAND $<TARGET_FILE:test_ansi_colors>
        COMMENT "Running ANSI colour tests..."
        USES_TERMINAL
    )
    add_custom_command(TARGET test_mouse POST_BUILD
        COMMAND $<TARGET_FILE:test_mouse>
        COMMENT "Running mouse unit tests..."
        USES_TERMINAL
    )
    add_dependencies(${PROJECT_NAME} test_parser test_scrollback test_screen test_ansi_colors test_mouse)
endif()
