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

# Standalone test executable (no raylib dependency).
add_executable(test_parser
    src/parser.c
    src/test_parser.c
)
target_include_directories(test_parser
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)
