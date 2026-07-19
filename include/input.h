/**
 * @file input.h
 * @brief GLFW keyboard input to terminal byte sequence converter.
 *
 * Polls raylib for all pending key events each frame and writes the
 * corresponding terminal escape sequences to the given file descriptor
 * (typically the master end of a PTY).
 */
#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include <GLFW/glfw3.h>

#define KEY_A GLFW_KEY_A
#define KEY_B GLFW_KEY_B
#define KEY_C GLFW_KEY_C
#define KEY_D GLFW_KEY_D
#define KEY_E GLFW_KEY_E
#define KEY_F GLFW_KEY_F
#define KEY_G GLFW_KEY_G
#define KEY_H GLFW_KEY_H
#define KEY_I GLFW_KEY_I
#define KEY_J GLFW_KEY_J
#define KEY_K GLFW_KEY_K
#define KEY_L GLFW_KEY_L
#define KEY_M GLFW_KEY_M
#define KEY_N GLFW_KEY_N
#define KEY_O GLFW_KEY_O
#define KEY_P GLFW_KEY_P
#define KEY_Q GLFW_KEY_Q
#define KEY_R GLFW_KEY_R
#define KEY_S GLFW_KEY_S
#define KEY_T GLFW_KEY_T
#define KEY_U GLFW_KEY_U
#define KEY_V GLFW_KEY_V
#define KEY_W GLFW_KEY_W
#define KEY_X GLFW_KEY_X
#define KEY_Y GLFW_KEY_Y
#define KEY_Z GLFW_KEY_Z
#define KEY_ENTER GLFW_KEY_ENTER
#define KEY_KP_ENTER GLFW_KEY_KP_ENTER
#define KEY_TAB GLFW_KEY_TAB
#define KEY_BACKSPACE GLFW_KEY_BACKSPACE
#define KEY_ESCAPE GLFW_KEY_ESCAPE
#define KEY_UP GLFW_KEY_UP
#define KEY_DOWN GLFW_KEY_DOWN
#define KEY_LEFT GLFW_KEY_LEFT
#define KEY_RIGHT GLFW_KEY_RIGHT
#define KEY_HOME GLFW_KEY_HOME
#define KEY_END GLFW_KEY_END
#define KEY_PAGE_UP GLFW_KEY_PAGE_UP
#define KEY_PAGE_DOWN GLFW_KEY_PAGE_DOWN
#define KEY_INSERT GLFW_KEY_INSERT
#define KEY_DELETE GLFW_KEY_DELETE
#define KEY_F1 GLFW_KEY_F1
#define KEY_F2 GLFW_KEY_F2
#define KEY_F3 GLFW_KEY_F3
#define KEY_F4 GLFW_KEY_F4
#define KEY_F5 GLFW_KEY_F5
#define KEY_F6 GLFW_KEY_F6
#define KEY_F7 GLFW_KEY_F7
#define KEY_F8 GLFW_KEY_F8
#define KEY_F9 GLFW_KEY_F9
#define KEY_F10 GLFW_KEY_F10
#define KEY_F11 GLFW_KEY_F11
#define KEY_F12 GLFW_KEY_F12
#define KEY_KP_0 GLFW_KEY_KP_0
#define KEY_KP_1 GLFW_KEY_KP_1
#define KEY_KP_2 GLFW_KEY_KP_2
#define KEY_KP_3 GLFW_KEY_KP_3
#define KEY_KP_4 GLFW_KEY_KP_4
#define KEY_KP_5 GLFW_KEY_KP_5
#define KEY_KP_6 GLFW_KEY_KP_6
#define KEY_KP_7 GLFW_KEY_KP_7
#define KEY_KP_8 GLFW_KEY_KP_8
#define KEY_KP_9 GLFW_KEY_KP_9
#define KEY_KP_DECIMAL GLFW_KEY_KP_DECIMAL
#define KEY_KP_DIVIDE GLFW_KEY_KP_DIVIDE
#define KEY_KP_MULTIPLY GLFW_KEY_KP_MULTIPLY
#define KEY_KP_SUBTRACT GLFW_KEY_KP_SUBTRACT
#define KEY_KP_ADD GLFW_KEY_KP_ADD
#define KEY_LEFT_ALT GLFW_KEY_LEFT_ALT
#define KEY_RIGHT_ALT GLFW_KEY_RIGHT_ALT
#define KEY_LEFT_SHIFT GLFW_KEY_LEFT_SHIFT
#define KEY_RIGHT_SHIFT GLFW_KEY_RIGHT_SHIFT
#define KEY_LEFT_CONTROL GLFW_KEY_LEFT_CONTROL
#define KEY_RIGHT_CONTROL GLFW_KEY_RIGHT_CONTROL

/**
 * Maximum length (in bytes) of a single terminal escape sequence generated
 * by one key press (e.g. "\x1B[1;5A" for Ctrl+ArrowUp is 7 bytes).
 */
#define INPUT_MAX_SEQ 16

/**
 * Convert a single raylib key + modifier state into a terminal byte
 * sequence.
 *
 * @param raylib_key  The key from raylib (e.g. KEY_A, KEY_UP, …)
 * @param shift       Whether Shift is held
 * @param ctrl        Whether Ctrl is held
 * @param alt         Whether Alt is held
 * @param out         Buffer (size INPUT_MAX_SEQ) to receive the sequence
 * @return            Number of bytes written to out (0 if unmapped / ignored)
 */
int key_to_seq(int raylib_key, bool shift, bool ctrl, bool alt,
               uint8_t out[INPUT_MAX_SEQ]);

/**
 * Poll raylib for all queued key presses (GetKeyPressed) and write the
 * resulting terminal sequences to `fd`.  Also checks held modifier keys
 * (Shift, Ctrl, Alt) at the time of each press.
 *
 * @param fd  File descriptor to write to (e.g. master end of a PTY).
 * @return    Total number of bytes written, or -1 on write error.
 */
int process_keyboard_input(int fd);

void input_init(GLFWwindow *window);
bool input_key_pressed(int key);
bool input_key_down(int key);

#endif // INPUT_H
