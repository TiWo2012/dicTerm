/**
 * @file clipboard.c
 * @brief Clipboard integration implementation.
 *
 * Primary mechanism: GLFW's glfwSetClipboardString/glfwGetClipboardString
 * for the CLIPBOARD selection (works on both X11 and Wayland).
 *
 * For X11 PRIMARY selection (middle-click paste) we shell out to the
 * `xsel` command-line tool, which is widely available on Linux and avoids
 * any compile-time or ABI dependency on X11 libraries.
 */
#define _DEFAULT_SOURCE
#include "clipboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Static state
// ---------------------------------------------------------------------------

/** GLFW window handle – set once by clipboard_init(). */
static GLFWwindow *cb_window = NULL;

/** Scratch buffer for clipboard paste results (CLIPBOARD selection). */
static char paste_buf[CLIPBOARD_MAX_SIZE];

/** Scratch buffer for PRIMARY paste results. */
static char primary_paste_buf[CLIPBOARD_MAX_SIZE];

/**
 * @brief Write text to a subprocess stdin.
 * @return true if the subprocess exited successfully.
 */
static bool write_to_subprocess(const char *cmd, const char *text) {
    FILE *fp = popen(cmd, "w");
    if (!fp) return false;
    size_t len = strlen(text);
    size_t written = fwrite(text, 1, len, fp);
    int rc = pclose(fp);
    (void)written;
    return rc == 0;
}

/**
 * @brief Read text from a subprocess stdout.
 * @param buf   Output buffer.
 * @param size  Buffer size.
 * @return      Pointer to buf, or empty string on failure.
 */
static const char *read_from_subprocess(const char *cmd,
                                        char *buf, size_t size) {
    buf[0] = '\0';
    FILE *fp = popen(cmd, "r");
    if (!fp) return buf;
    size_t total = 0;
    while (total < size - 1) {
        size_t r = fread(buf + total, 1, size - 1 - total, fp);
        if (r == 0) break;
        total += r;
    }
    buf[total] = '\0';
    // Strip trailing newlines
    while (total > 0 && (buf[total - 1] == '\n' || buf[total - 1] == '\r'))
        buf[--total] = '\0';
    pclose(fp);
    return buf;
}

/** @brief Check if `xsel` is available and DISPLAY is set. */
static bool xsel_available(void) {
    const char *dpy = getenv("DISPLAY");
    if (!dpy || !dpy[0]) return false;
    // Check if xsel is available via `which`
    FILE *fp = popen("which xsel 2>/dev/null", "r");
    if (!fp) return false;
    char path[512];
    bool found = fgets(path, (int)sizeof(path), fp) != NULL;
    pclose(fp);
    return found;
}

// ---------------------------------------------------------------------------
// GLFW clipboard (CLIPBOARD selection)
// ---------------------------------------------------------------------------

void clipboard_init(GLFWwindow *window) {
    cb_window = window;
}

void clipboard_copy(const char *text) {
    if (!cb_window || !text) return;
    glfwSetClipboardString(cb_window, text);

    // Also try to set PRIMARY selection for middle-click convenience
    clipboard_copy_primary(text);
}

const char *clipboard_paste(void) {
    if (!cb_window) return "";
    const char *s = glfwGetClipboardString(cb_window);
    if (!s) return "";
    size_t len = strlen(s);
    if (len >= sizeof(paste_buf))
        len = sizeof(paste_buf) - 1;
    memcpy(paste_buf, s, len);
    paste_buf[len] = '\0';
    return paste_buf;
}

// ---------------------------------------------------------------------------
// X11 PRIMARY selection (via xsel)
// ---------------------------------------------------------------------------

void clipboard_copy_primary(const char *text) {
    if (!text || !text[0]) return;
    if (!xsel_available()) return;
    write_to_subprocess("xsel -p -i 2>/dev/null", text);
}

const char *clipboard_paste_primary(void) {
    if (!xsel_available()) return "";
    return read_from_subprocess("xsel -p -o 2>/dev/null",
                                primary_paste_buf,
                                sizeof(primary_paste_buf));
}

bool clipboard_primary_available(void) {
    return xsel_available();
}

// ---------------------------------------------------------------------------
// Base64 decode
// ---------------------------------------------------------------------------

int base64_decode(const char *in, uint8_t *out, int out_size) {
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static int rev[256];
    static bool rev_init = false;
    if (!rev_init) {
        for (int i = 0; i < 256; i++) rev[i] = -1;
        for (int i = 0; i < 64; i++) rev[(int)b64[i]] = i;
        rev_init = true;
    }

    int out_pos = 0;
    unsigned int buf = 0;
    int bits = 0;
    for (const char *p = in; *p && out_pos < out_size; p++) {
        unsigned int c = (unsigned char)*p;
        if (c == '=') break;
        int v = rev[(int)c];
        if (v < 0) continue;
        buf = (buf << 6) | (unsigned int)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out_pos < out_size)
                out[out_pos++] = (uint8_t)(buf >> bits);
        }
    }
    return out_pos;
}
