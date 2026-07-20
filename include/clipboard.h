/**
 * @file clipboard.h
 * @brief Clipboard integration for Linux via GLFW.
 *
 * Uses GLFW's built-in clipboard API (glfwSetClipboardString /
 * glfwGetClipboardString) for the CLIPBOARD selection (Ctrl+C / Ctrl+V),
 * which works transparently on X11 and Wayland.
 *
 * X11 PRIMARY selection (middle-click paste) is supported via the
 * `xsel` command-line tool when available at runtime.
 */
#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <stdbool.h>
#include <GLFW/glfw3.h>

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

/**
 * @brief Initialise the clipboard subsystem.
 *
 * Must be called once after the GLFW window is created.
 *
 * @param window  A valid GLFW window handle.
 */
void clipboard_init(GLFWwindow *window);

// ---------------------------------------------------------------------------
// CLIPBOARD selection (Ctrl+C / Ctrl+V)
// ---------------------------------------------------------------------------

/**
 * @brief Copy a NUL-terminated UTF-8 string to the CLIPBOARD selection.
 *
 * Sets the system clipboard (Ctrl+C / Ctrl+V) via GLFW.
 * Also attempts to set the X11 PRIMARY selection via xsel.
 *
 * @param text  UTF-8 string to copy.
 */
void clipboard_copy(const char *text);

/**
 * @brief Retrieve text from the CLIPBOARD selection.
 *
 * Returns a pointer to internal storage – copy it immediately if you
 * need it across subsequent calls.  Returns an empty string (never
 * NULL) on failure.
 *
 * @return  NUL-terminated UTF-8 string from the clipboard.
 */
const char *clipboard_paste(void);

// ---------------------------------------------------------------------------
// PRIMARY selection (middle-click paste on X11)
// ---------------------------------------------------------------------------

/**
 * @brief Copy text to the X11 PRIMARY selection (middle-click buffer).
 *
 * Uses the `xsel` command-line tool.  On Wayland or if xsel is not
 * installed this is a silent no-op.
 *
 * @param text  UTF-8 string to copy.
 */
void clipboard_copy_primary(const char *text);

/**
 * @brief Retrieve text from the X11 PRIMARY selection.
 *
 * Uses the `xsel` command-line tool.  On Wayland or if xsel is not
 * installed this returns an empty string.
 *
 * @return  NUL-terminated UTF-8 string from the PRIMARY selection.
 */
const char *clipboard_paste_primary(void);

/**
 * @brief Check whether the X11 PRIMARY selection is available.
 *
 * @return true if `xsel` is found in PATH and DISPLAY is set.
 */
bool clipboard_primary_available(void);

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

/**
 * @brief Decode a base64 string into raw bytes.
 *
 * Skips non-base64 characters (whitespace, newlines).
 * Stops at padding '=' characters.
 *
 * @param in       NUL-terminated base64 input string.
 * @param out      Output buffer for decoded bytes.
 * @param out_size Maximum number of bytes to write to @p out.
 * @return         Number of decoded bytes written to @p out.
 */
int base64_decode(const char *in, uint8_t *out, int out_size);

// ---------------------------------------------------------------------------
// Buffer limits
// ---------------------------------------------------------------------------

/** @brief Maximum size of a single clipboard transfer (64 KB). */
#define CLIPBOARD_MAX_SIZE 65536

#endif // CLIPBOARD_H
