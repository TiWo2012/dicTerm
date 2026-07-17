#ifndef INPUT_H
#define INPUT_H

// ---------------------------------------------------------------------------
// Raylib keyboard input → terminal byte sequence converter
//
// Polls raylib for all pending key events each frame and writes the
// corresponding terminal escape sequences to the given file descriptor
// (typically the master end of a PTY).
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>

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

#endif // INPUT_H
