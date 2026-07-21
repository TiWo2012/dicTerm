cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_COMPILER clang CACHE STRING "C compiler" FORCE)

project(dicTerm LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Compile options shared by all targets ──────────────────────────────────
set(COMMON_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wundef
    -Wcast-align
    -Wcast-qual
    -Wwrite-strings
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wmissing-declarations
    -Wold-style-definition
    -Wdouble-promotion
    -Wfloat-equal
    -Wformat=2
    -Wformat-security
    -Wnull-dereference
    -Wimplicit-fallthrough
    -Wswitch-enum
    -Wswitch-default
    -Wunreachable-code
    -Wvla
    -Wpointer-arith
    -Wredundant-decls
    -Wbad-function-cast
    -Wnested-externs
    -Wunused-macros
    -Wuninitialized
    -Winit-self
    -Wdocumentation
    -fstrict-flex-arrays=3
    -fstack-protector-strong
    -fno-common
)

string(REPLACE ";" " " COMMON_WARNINGS_STR "${COMMON_WARNINGS}")

set(COMMON_C_FLAGS "-std=c2y ${COMMON_WARNINGS_STR}")

# Per-configuration flags
set(CMAKE_C_FLAGS_DEBUG
    "-g3 -Og -fsanitize=address,undefined -fno-omit-frame-pointer ${COMMON_C_FLAGS}")

set(CMAKE_C_FLAGS_RELEASE
    "-O2 -DNDEBUG ${COMMON_C_FLAGS}")

set(CMAKE_C_FLAGS_RELWITHDEBINFO
    "-O2 -g3 ${COMMON_C_FLAGS}")

set(CMAKE_C_FLAGS_MINSIZEREL
    "-Os -DNDEBUG ${COMMON_C_FLAGS}")

# Default to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(glfw3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE2 REQUIRED freetype2)
pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

# ── Include path helper ────────────────────────────────────────────────────
set(INC_DIR ${CMAKE_SOURCE_DIR}/include)

# ── Libraries ──────────────────────────────────────────────────────────────
# libcore: pure C modules with no raylib dependency.
#   parser  – ECMA-48 escape sequence state machine
#   screen  – screen cell buffer with SGR attributes
#   scrollback – ring-buffer line history
add_library(core STATIC
    src/config.c
    src/parser.c
    src/screen.c
    src/scrollback.c
)
target_include_directories(core PUBLIC ${INC_DIR})

# libtty: terminal I/O modules that depend on raylib.
#   font     – glyph-atlas font rendering, multi-font fallback
#   input    – keyboard event → escape-sequence conversion
#   clipboard – GLFW + X11 clipboard integration
add_library(tty STATIC
    src/font.c
    src/gl_renderer.c
    src/input.c
    src/clipboard.c
)
target_include_directories(tty PUBLIC ${INC_DIR})
target_include_directories(tty PRIVATE ${FREETYPE2_INCLUDE_DIRS} ${HARFBUZZ_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})
target_link_libraries(tty PUBLIC glfw ${FREETYPE2_LIBRARIES} ${HARFBUZZ_LIBRARIES} ${FONTCONFIG_LIBRARIES} GL m ${CMAKE_DL_LIBS})

# ── Main executable ────────────────────────────────────────────────────────
add_executable(${PROJECT_NAME} src/main.c)
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        core
        tty
)

# ── Helper: set compile options and includes for a test target ─────────────
function(add_dicterm_test NAME)
    add_executable(${NAME} ${ARGN})
    target_include_directories(${NAME}
        PRIVATE
            ${INC_DIR}
    )
    # Keep assertions active in all build types so test validation survives
    # a Release (-DNDEBUG) build and the suite still compiles cleanly.
    target_compile_options(${NAME} PRIVATE -UNDEBUG)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

# ── Test executables ───────────────────────────────────────────────────────
# Tests against libtty (need GLFW for clipboard module).
add_dicterm_test(test_clipboard
    src/test_clipboard.c
)
target_link_libraries(test_clipboard PRIVATE tty)

# Tests against libcore (no raylib needed).
add_dicterm_test(test_parser
    src/test_parser.c
)
target_link_libraries(test_parser PRIVATE core)

add_dicterm_test(test_config
    src/test_config.c
)
target_link_libraries(test_config PRIVATE core)

add_dicterm_test(test_scrollback
    src/test_scrollback.c
)
target_link_libraries(test_scrollback PRIVATE core)

add_dicterm_test(test_screen
    src/test_screen.c
)
target_link_libraries(test_screen PRIVATE core)

add_dicterm_test(test_ansi_colors
    src/test_ansi_colors.c
)
target_link_libraries(test_ansi_colors PRIVATE core)

add_dicterm_test(test_integration
    src/test_integration.c
)
target_link_libraries(test_integration PRIVATE core)

add_dicterm_test(test_terminal
    src/test_terminal.c
)
target_link_libraries(test_terminal PRIVATE core)

# Tests against libtty (need raylib).
add_dicterm_test(test_input
    src/test_input.c
)
target_link_libraries(test_input PRIVATE tty)

add_dicterm_test(test_font_codepoints
    src/test_font_codepoints.c
)
target_link_libraries(test_font_codepoints PRIVATE tty)

# ── CTest registration ────────────────────────────────────────────────────
enable_testing()

# ── Auto-run tests in Debug builds ────────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    foreach(tgt test_clipboard test_parser test_config test_scrollback test_screen test_ansi_colors
                 test_integration test_terminal test_input test_font_codepoints)
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND $<TARGET_FILE:${tgt}>
            COMMENT "Running ${tgt}..."
            USES_TERMINAL
        )
    endforeach()
    add_dependencies(${PROJECT_NAME}
        test_clipboard test_parser test_config test_scrollback test_screen test_ansi_colors
        test_integration test_terminal test_input test_font_codepoints
    )
endif()
