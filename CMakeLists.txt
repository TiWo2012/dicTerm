cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_COMPILER clang CACHE STRING "C compiler" FORCE)

project(dicTerm LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c2y -Wall -Wextra -Werror -g")

file(GLOB_RECURSE C_SOURCES CONFIGURE_DEPENDS src/*.c)

add_executable(${PROJECT_NAME} ${C_SOURCES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)
