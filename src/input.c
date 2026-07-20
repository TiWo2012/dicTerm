/**
 * @file input.c
 * @brief Keyboard input implementation.
 *
 * Converts raylib key events into terminal escape sequences and writes
 * them to the master end of a PTY.  Handles printable characters (via
 * GetCharPressed), C0 controls (Ctrl+letter), cursor keys, function keys,
 * keypad, and modifier combinations (Ctrl, Alt, Shift).
 */
#include "input.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Key-to-sequence conversion
// ---------------------------------------------------------------------------

// Helper: write all bytes from `buf` (len bytes) to `fd`,
// retrying on partial writes.  Returns the number of bytes
// written on success, or -1 on a real error.
static int write_all(int fd, const uint8_t *buf, int len) {
  int total = 0;
  while (total < len) {
    ssize_t r = write(fd, buf + total, (size_t)(len - total));
    if (r > 0) {
      total += (int)r;
    } else if (r == 0) {
      // Unexpected from a PTY master; treat as error.
      return -1;
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        continue; // try again (non-blocking fd)
      return -1;
    }
  }
  return total;
}

/**
 * @brief Convert a raylib key event to a terminal byte sequence.
 *
 * Handles Ctrl+letter (→ 0x01–0x1A), Alt+key (→ ESC prefix + key),
 * arrow keys (with Ctrl modifier), Home/End, Page Up/Down, Insert/Delete,
 * function keys (xterm-style), and keypad digits/operators.
 *
 * @param raylib_key  The raylib key constant (KEY_A, KEY_UP, ...).
 * @param shift       Whether Shift is held (currently unused for special keys).
 * @param ctrl        Whether Ctrl is held.
 * @param alt         Whether Alt is held.
 * @param out         Output buffer (INPUT_MAX_SEQ bytes), zeroed on entry.
 * @return            Number of bytes written to out (0 if unmapped).
 */
int key_to_seq(int raylib_key, bool shift, bool ctrl, bool alt,
               uint8_t out[INPUT_MAX_SEQ]) {
  (void)shift;
  // Shift for printable characters is handled by GetCharPressed().
  // GetKeyPressed() does NOT return different values for shifted keys;
  // for special keys (arrows, F-keys, etc.) we currently don't encode
  // the Shift modifier (a future enhancement).

  memset(out, 0, INPUT_MAX_SEQ);
  int pos = 0;

  // Helper: append a byte, bounds-checking.
#define APPEND(b)                   \
  do {                              \
    if (pos < INPUT_MAX_SEQ - 1)    \
      out[pos++] = (uint8_t)(b);    \
  } while (0)

  // --- Ctrl + letter (0x01 – 0x1A) ---
  if (ctrl && raylib_key >= KEY_A && raylib_key <= KEY_Z) {
    // Ctrl+A = 1, Ctrl+B = 2, … Ctrl+Z = 26
    APPEND((uint8_t)(raylib_key - KEY_A + 1));
    goto done;
  }

  // --- Alt handling (prefix with ESC) ---
  // If Alt is held without Ctrl, we prefix the sequence with ESC.
  // The actual key is processed below; Alt+letter sends ESC + letter.
  // For special keys (arrows, etc.) this combines with the sequence below
  // to produce the double-ESC convention (e.g. Alt+Up → \x1B\x1B[A).
  if (alt && !ctrl) {
    APPEND(0x1B); // ESC prefix
  }

  // --- Printable keys (handled via GetCharPressed in the main loop) ---
  // The process_keyboard_input function handles printable characters
  // separately via GetCharPressed. Here we handle everything else.

  switch (raylib_key) {

  // ---- Enter / Return ----
  case KEY_ENTER:
  case KEY_KP_ENTER:
    APPEND('\r'); // CR – terminal expects \r, shell converts to LF
    break;

  // ---- Tab ----
  case KEY_TAB:
    APPEND('\t');
    break;

  // ---- Backspace ----
  case KEY_BACKSPACE:
    APPEND(0x7F); // DEL (common for modern terminal emulators)
    break;

  // ---- Escape ----
  case KEY_ESCAPE:
    // If Alt was already handled above, ESC alone sends just ESC.
    if (!alt) {
      APPEND(0x1B);
    }
    break;

  // ---- Arrow keys ----
  // Alt variants fall through to the `else` branch; the generic
  // `alt && !ctrl` handler above already prepended \x1B, producing
  // the double-ESC convention (\x1B\x1B[A for Alt+Up).
  case KEY_UP:
    if (ctrl) {
      APPEND(0x1B); APPEND('['); APPEND('1'); APPEND(';'); APPEND('5'); APPEND('A');
    } else {
      APPEND(0x1B); APPEND('['); APPEND('A');
    }
    break;
  case KEY_DOWN:
    if (ctrl) {
      APPEND(0x1B); APPEND('['); APPEND('1'); APPEND(';'); APPEND('5'); APPEND('B');
    } else {
      APPEND(0x1B); APPEND('['); APPEND('B');
    }
    break;
  case KEY_RIGHT:
    if (ctrl) {
      APPEND(0x1B); APPEND('['); APPEND('1'); APPEND(';'); APPEND('5'); APPEND('C');
    } else {
      APPEND(0x1B); APPEND('['); APPEND('C');
    }
    break;
  case KEY_LEFT:
    if (ctrl) {
      APPEND(0x1B); APPEND('['); APPEND('1'); APPEND(';'); APPEND('5'); APPEND('D');
    } else {
      APPEND(0x1B); APPEND('['); APPEND('D');
    }
    break;

  // ---- Home / End (use application-mode style \x1B[H / \x1B[F) ----
  case KEY_HOME:
    APPEND(0x1B); APPEND('['); APPEND('H');
    break;
  case KEY_END:
    APPEND(0x1B); APPEND('['); APPEND('F');
    break;

  // ---- Page Up / Page Down ----
  case KEY_PAGE_UP:
    APPEND(0x1B); APPEND('['); APPEND('5'); APPEND('~');
    break;
  case KEY_PAGE_DOWN:
    APPEND(0x1B); APPEND('['); APPEND('6'); APPEND('~');
    break;

  // ---- Insert / Delete ----
  case KEY_INSERT:
    APPEND(0x1B); APPEND('['); APPEND('2'); APPEND('~');
    break;
  case KEY_DELETE:
    APPEND(0x1B); APPEND('['); APPEND('3'); APPEND('~');
    break;

  // ---- Function keys (xterm-style \x1B[N~ or \x1BOx) ----
  case KEY_F1:
    APPEND(0x1B); APPEND('O'); APPEND('P');
    break;
  case KEY_F2:
    APPEND(0x1B); APPEND('O'); APPEND('Q');
    break;
  case KEY_F3:
    APPEND(0x1B); APPEND('O'); APPEND('R');
    break;
  case KEY_F4:
    APPEND(0x1B); APPEND('O'); APPEND('S');
    break;
  case KEY_F5:
    APPEND(0x1B); APPEND('['); APPEND('1'); APPEND('5'); APPEND('~');
    break;
  case KEY_F6:
    APPEND(0x1B); APPEND('['); APPEND('1'); APPEND('7'); APPEND('~');
    break;
  case KEY_F7:
    APPEND(0x1B); APPEND('['); APPEND('1'); APPEND('8'); APPEND('~');
    break;
  case KEY_F8:
    APPEND(0x1B); APPEND('['); APPEND('1'); APPEND('9'); APPEND('~');
    break;
  case KEY_F9:
    APPEND(0x1B); APPEND('['); APPEND('2'); APPEND('0'); APPEND('~');
    break;
  case KEY_F10:
    APPEND(0x1B); APPEND('['); APPEND('2'); APPEND('1'); APPEND('~');
    break;
  case KEY_F11:
    APPEND(0x1B); APPEND('['); APPEND('2'); APPEND('3'); APPEND('~');
    break;
  case KEY_F12:
    APPEND(0x1B); APPEND('['); APPEND('2'); APPEND('4'); APPEND('~');
    break;

  // ---- Keypad keys ----
  case KEY_KP_0: APPEND('0'); break;
  case KEY_KP_1: APPEND('1'); break;
  case KEY_KP_2: APPEND('2'); break;
  case KEY_KP_3: APPEND('3'); break;
  case KEY_KP_4: APPEND('4'); break;
  case KEY_KP_5: APPEND('5'); break;
  case KEY_KP_6: APPEND('6'); break;
  case KEY_KP_7: APPEND('7'); break;
  case KEY_KP_8: APPEND('8'); break;
  case KEY_KP_9: APPEND('9'); break;
  case KEY_KP_DECIMAL:
    APPEND('.');
    break;
  case KEY_KP_DIVIDE:
    APPEND('/');
    break;
  case KEY_KP_MULTIPLY:
    APPEND('*');
    break;
  case KEY_KP_SUBTRACT:
    APPEND('-');
    break;
  case KEY_KP_ADD:
    APPEND('+');
    break;


  default:
    // If we reach here and the key is a letter/digit that wasn't handled
    // under Ctrl above, it might be a simple character. But those are
    // typically handled via GetCharPressed in the main loop, so we
    // don't re-emit them here.
    break;
  }

#undef APPEND

done:
  return pos;
}

// ---------------------------------------------------------------------------
// GLFW event queue and PTY forwarding
// ---------------------------------------------------------------------------

static GLFWwindow *input_window;
static int key_queue[256];
static int key_queue_count;
static unsigned int char_queue[256];
static int char_queue_count;
static bool pressed[GLFW_KEY_LAST + 1];

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
  (void)window; (void)scancode; (void)mods;
  if (key >= 0 && key <= GLFW_KEY_LAST && action == GLFW_PRESS) pressed[key] = true;
  if (action == GLFW_PRESS && key_queue_count < 256) key_queue[key_queue_count++] = key;
}

static void char_callback(GLFWwindow *window, unsigned int codepoint) {
  (void)window;
  if (char_queue_count < 256) char_queue[char_queue_count++] = codepoint;
}

void input_init(GLFWwindow *window) {
  input_window = window;
  glfwSetKeyCallback(window, key_callback);
  glfwSetCharCallback(window, char_callback);
}

bool input_key_pressed(int key) {
  bool value = key >= 0 && key <= GLFW_KEY_LAST && pressed[key];
  if (key >= 0 && key <= GLFW_KEY_LAST) pressed[key] = false;
  return value;
}

bool input_key_down(int key) {
  return input_window && glfwGetKey(input_window, key) == GLFW_PRESS;
}

/**
 * @brief Remove a key from the pending key queue so it won't be forwarded
 *        to the PTY.  Used to intercept clipboard shortcuts (Ctrl+Shift+C,
 *        Ctrl+Shift+V, Shift+Insert) in main.c.
 *
 * @param key  The GLFW key to remove (e.g. GLFW_KEY_C, GLFW_KEY_V).
 */
void input_consume_key(int key) {
  for (int i = 0; i < key_queue_count; i++) {
    if (key_queue[i] == key) {
      // Shift remaining entries down
      int remaining = key_queue_count - i - 1;
      if (remaining > 0)
        memmove(&key_queue[i], &key_queue[i + 1],
                (size_t)remaining * sizeof(key_queue[0]));
      key_queue_count--;
      pressed[key] = false;
      break;
    }
  }
}

/**
 * @brief Poll raylib and write all pending key events to a file descriptor.
 *
 * Two-phase processing:
 *   1. Printable characters via GetCharPressed() — handles letters, digits,
 *      symbols with proper Shift/Caps state.  C0 controls and DEL are
 *      skipped (they come via GetKeyPressed instead).
 *   2. Non-character and control keys via GetKeyPressed() — delegates to
 *      key_to_seq() for each pressed key.
 *
 * Scroll keys (Page Up/Down, Shift+Up/Down) are intentionally NOT
 * forwarded to the PTY — they are handled locally by the scrollback
 * viewport logic in main.c.
 *
 * @param fd  File descriptor to write to (master end of PTY).
 * @return    Total bytes written, or -1 on write error.
 */
int process_keyboard_input(int fd) {
  int total_written = 0;
  uint8_t seq[INPUT_MAX_SEQ];

  // ---------------------------------------------------------------
  // 1. Printable characters via GetCharPressed()
  //    This handles letters, digits, symbols with proper shift/caps.
  //    Control characters (c < 0x20) and DEL (0x7F) are NOT handled
  //    here to avoid double-emitting them when GetKeyPressed() also
  //    reports them.  They are instead processed in step 2.
  // ---------------------------------------------------------------
  int safety = char_queue_count;
  for (int qi = 0; qi < safety; qi++) {
    int c = (int)char_queue[qi];
    // Skip C0 controls (0x00-0x1F) and DEL (0x7F) — these come via
    // GetKeyPressed() instead.
    if ((c < 0x20 && c != '\n') || c == 0x7F)
      continue;
    // Treat '\n' (0x0A) as a printable?  Some backends send it.
    // We still skip it since KEY_ENTER is handled in key_to_seq.

    // Encode the codepoint to UTF-8.
    uint8_t utf8[8];
    int utf8_len = 0;

    if (c < 0x80) {
      utf8[utf8_len++] = (uint8_t)c;
    } else if (c < 0x800) {
      utf8[utf8_len++] = (uint8_t)(0xC0 | (c >> 6));
      utf8[utf8_len++] = (uint8_t)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      utf8[utf8_len++] = (uint8_t)(0xE0 | (c >> 12));
      utf8[utf8_len++] = (uint8_t)(0x80 | ((c >> 6) & 0x3F));
      utf8[utf8_len++] = (uint8_t)(0x80 | (c & 0x3F));
    } else if (c < 0x110000) {
      utf8[utf8_len++] = (uint8_t)(0xF0 | (c >> 18));
      utf8[utf8_len++] = (uint8_t)(0x80 | ((c >> 12) & 0x3F));
      utf8[utf8_len++] = (uint8_t)(0x80 | ((c >> 6) & 0x3F));
      utf8[utf8_len++] = (uint8_t)(0x80 | (c & 0x3F));
    }

    if (utf8_len > 0) {
      // Alt prefix
      bool alt_held = input_key_down(KEY_LEFT_ALT) || input_key_down(KEY_RIGHT_ALT);
      if (alt_held) {
        uint8_t alt_buf[INPUT_MAX_SEQ];
        int alt_pos = 0;
        alt_buf[alt_pos++] = 0x1B; // ESC prefix
        for (int i = 0; i < utf8_len && alt_pos < INPUT_MAX_SEQ - 1; i++) {
          alt_buf[alt_pos++] = utf8[i];
        }
        if (write_all(fd, alt_buf, alt_pos) < 0) return -1;
        total_written += alt_pos;
      } else {
        if (write_all(fd, utf8, utf8_len) < 0) return -1;
        total_written += utf8_len;
      }
    }
  }

  // ---------------------------------------------------------------
  // 2. Non-character and control keys via GetKeyPressed()
  // ---------------------------------------------------------------
  bool shift_held, ctrl_held, alt_held;
  for (int qi = 0; qi < key_queue_count; qi++) {
    int key = key_queue[qi];
    // Check modifiers at the time of this poll.
    shift_held = input_key_down(KEY_LEFT_SHIFT) || input_key_down(KEY_RIGHT_SHIFT);
    ctrl_held  = input_key_down(KEY_LEFT_CONTROL) || input_key_down(KEY_RIGHT_CONTROL);
    alt_held   = input_key_down(KEY_LEFT_ALT) || input_key_down(KEY_RIGHT_ALT);

    // Scroll keys are handled locally in main.c – do NOT forward to PTY.
    if (key == KEY_PAGE_UP || key == KEY_PAGE_DOWN ||
        ((key == KEY_UP || key == KEY_DOWN) && shift_held))
      continue;

    int len = key_to_seq(key, shift_held, ctrl_held, alt_held, seq);
    if (len > 0) {
      if (write_all(fd, seq, len) < 0) return -1;
      total_written += len;
    }
  }

  char_queue_count = 0;
  key_queue_count = 0;
  return total_written;
}

// ===========================================================================
// Mouse input handling
// ===========================================================================

/**
 * @file input.c (mouse section)
 * @brief Mouse input implementation.
 *
 * Converts GLFW mouse events into terminal escape sequences using the
 * active mouse tracking protocol (X10, VT200, SGR, urxvt, SGR pixels).
 * Handles button press/release, motion (when dragging), and wheel events.
 */

#define MOUSE_EVENT_QUEUE 256

/** @brief Internal mouse event queue entry. */
typedef struct {
  int   type;      /**< 0=button, 1=motion, 2=scroll, 3=leave. */
  int   button;    /**< GLFW_MOUSE_BUTTON_1/2/3 etc. */
  int   action;    /**< GLFW_PRESS / GLFW_RELEASE. */
  int   mods;      /**< GLFW_MOD_* flags at time of event. */
  double x, y;     /**< Window-relative cursor position (pixels). */
  double sx, sy;   /**< Scroll offsets. */
} mouse_qevent_t;

// --- Static state -----------------------------------------------------------

static GLFWwindow        *mouse_window;
static mouse_tracking_t   mouse_tracking = MOUSE_TRACK_OFF;
static mouse_encoding_t   mouse_encoding = MOUSE_ENC_DEFAULT;
static int                mouse_term_cols  = 80;
static int                mouse_term_rows  = 24;
static double             mouse_char_w     = 10.0;
static double             mouse_char_h     = 20.0;
static int                mouse_padding    = 10;

static mouse_qevent_t     mouse_queue[MOUSE_EVENT_QUEUE];
static int                mouse_qcount;

// Track button state for motion events.
static bool mouse_btn_state[3];   /**< Indexed by button (0=left, 1=right, 2=middle). */
static int  mouse_last_btn = -1;  /**< Most recently pressed button. */

/** @brief Write a string literal directly to fd (no trailing NUL written). */
static int mouse_write_str(int fd, const char *s) {
  size_t len = strlen(s);
  return write_all(fd, (const uint8_t *)s, (int)len);
}

/**
 * @brief Determine whether SGR-style encoding (with '<' prefix and 'M'/'m' final)
 *        should be used for the current encoding mode.
 */
static bool mouse_enc_is_sgr_style(void) {
  return mouse_encoding == MOUSE_ENC_SGR || mouse_encoding == MOUSE_ENC_SGR_PIX;
}

/**
 * @brief Determine whether coordinates are encoded in the old +32 offset style.
 */
static bool mouse_enc_is_offset(void) {
  return mouse_encoding == MOUSE_ENC_DEFAULT;
}

// ---------------------------------------------------------------------------
// Coordinate conversion
// ---------------------------------------------------------------------------

/**
 * @brief Convert window pixel coordinates to 1-based terminal cell coords.
 *
 * Returns false if the position is outside the terminal grid.
 */
static bool pixel_to_cell(double px, double py, int *col, int *row) {
  double cx = (px - mouse_padding) / mouse_char_w;
  double cy = (py - mouse_padding) / mouse_char_h;
  if (cx < 0.0) cx = 0.0;
  if (cy < 0.0) cy = 0.0;
  int c = (int)cx;
  int r = (int)cy;
  if (c >= mouse_term_cols) c = mouse_term_cols - 1;
  if (r >= mouse_term_rows) r = mouse_term_rows - 1;
  if (c < 0 || r < 0) return false;
  *col = c + 1; // 1-based
  *row = r + 1;
  return true;
}

// ---------------------------------------------------------------------------
// Button code encoding
// ---------------------------------------------------------------------------

/**
 * @brief Compute the button code for a mouse event.
 *
 * Encoding (cb):
 *   0 = left, 1 = middle, 2 = right, 3 = release
 *   +4 = shift, +8 = meta/alt, +16 = ctrl, +32 = motion
 *   +64 = scroll up, +65 = scroll down
 */
static int encode_button(int glfw_btn, int action, int mods, bool is_motion) {
  int cb;
  if (glfw_btn == GLFW_MOUSE_BUTTON_LEFT)   cb = 0;
  else if (glfw_btn == GLFW_MOUSE_BUTTON_MIDDLE) cb = 1;
  else if (glfw_btn == GLFW_MOUSE_BUTTON_RIGHT)  cb = 2;
  else cb = 0;

  if (action == GLFW_RELEASE) {
    cb = 3; // release code
  }

  // Motion flag
  if (is_motion) cb |= 32;

  // Modifier flags
  if (mods & GLFW_MOD_SHIFT)   cb |= 4;
  if (mods & GLFW_MOD_ALT)     cb |= 8;
  if (mods & GLFW_MOD_CONTROL) cb |= 16;

  return cb;
}

// ---------------------------------------------------------------------------
// GLFW callbacks
// ---------------------------------------------------------------------------

static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods) {
  (void)window;
  if (mouse_qcount >= MOUSE_EVENT_QUEUE) return;

  // Only handle left, right, middle
  int idx = -1;
  if (button == GLFW_MOUSE_BUTTON_LEFT)   idx = 0;
  else if (button == GLFW_MOUSE_BUTTON_RIGHT)  idx = 1;
  else if (button == GLFW_MOUSE_BUTTON_MIDDLE) idx = 2;
  else return;

  double x, y;
  glfwGetCursorPos(window, &x, &y);

  mouse_qevent_t *ev = &mouse_queue[mouse_qcount++];
  ev->type   = 0; // button
  ev->button = button;
  ev->action = action;
  ev->mods   = mods;
  ev->x      = x;
  ev->y      = y;
  ev->sx     = 0;
  ev->sy     = 0;

  mouse_btn_state[idx] = (action == GLFW_PRESS);
  if (action == GLFW_PRESS) mouse_last_btn = button;
}

static void mouse_move_callback(GLFWwindow *window, double x, double y) {
  (void)window;
  if (mouse_qcount >= MOUSE_EVENT_QUEUE) return;

  mouse_qevent_t *ev = &mouse_queue[mouse_qcount++];
  ev->type   = 1; // motion
  ev->button = -1;
  ev->action = -1;
  ev->mods   = 0;
  ev->x      = x;
  ev->y      = y;
  ev->sx     = 0;
  ev->sy     = 0;
}

static void mouse_scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
  (void)window; (void)xoffset;
  if (mouse_qcount >= MOUSE_EVENT_QUEUE) return;

  mouse_qevent_t *ev = &mouse_queue[mouse_qcount++];
  ev->type   = 2; // scroll
  ev->button = -1;
  ev->action = -1;
  ev->mods   = 0;
  ev->x      = 0;
  ev->y      = 0;
  ev->sx     = 0;
  ev->sy     = yoffset;
}

static void mouse_enter_callback(GLFWwindow *window, int entered) {
  (void)window;
  if (mouse_qcount >= MOUSE_EVENT_QUEUE) return;

  mouse_qevent_t *ev = &mouse_queue[mouse_qcount++];
  ev->type   = 3; // leave
  ev->button = -1;
  ev->action = entered;
  ev->mods   = 0;
  ev->x      = 0;
  ev->y      = 0;
  ev->sx     = 0;
  ev->sy     = 0;
}

// ---------------------------------------------------------------------------
// Sequence builders
// ---------------------------------------------------------------------------

/**
 * @brief Maximum buffer size for a mouse escape sequence.
 *
 * Largest: SGR pixel mode
 *   ESC[<255;65535;65535M  (24 bytes)  plus NUL.
 */
#define MOUSE_SEQ_MAX 64

/**
 * @brief Build a mouse event escape sequence.
 *
 * @param out        Output buffer (MOUSE_SEQ_MAX bytes, NUL-terminated).
 * @param cb         Button code (encoded, with modifiers already OR'd in).
 * @param col        1-based terminal column.
 * @param row        1-based terminal row.
 * @param is_release true if this is a button release.
 * @param is_motion  true if this is a motion event.
 * @return           Number of bytes written to out (excluding NUL).
 */
static int build_mouse_seq(char out[MOUSE_SEQ_MAX], int cb,
                           int col, int row, bool is_release, bool is_motion) {
  out[0] = '\0';

  int n_col = col;
  int n_row = row;

  if (is_motion) {
    cb |= 32; // motion flag
  }

  if (mouse_enc_is_offset()) {
    // Offset encoding (old style): coordinates are 1-based, encoded as +32
    // Range 1-223 only.
    if (n_col > 223) n_col = 223;
    if (n_row > 223) n_row = 223;
    int len = snprintf(out, MOUSE_SEQ_MAX, "\x1B[M%c%c%c",
                       (char)(cb + 32), (char)(n_col + 32), (char)(n_row + 32));
    return (len > 0) ? len : 0;
  }

  if (mouse_enc_is_sgr_style() || mouse_encoding == MOUSE_ENC_SGR_PIX) {
    // SGR or SGR-pixel encoding: ESC [ < cb ; col ; row M/m
    if (mouse_encoding == MOUSE_ENC_SGR_PIX) {
      // SGR pixels: use pixel coordinates
      int px = (int)((double)(col - 1) * mouse_char_w + mouse_char_w / 2.0);
      int py = (int)((double)(row - 1) * mouse_char_h + mouse_char_h / 2.0);
      if (is_release) {
        int len = snprintf(out, MOUSE_SEQ_MAX, "\x1B[<%d;%d;%dm", cb, px, py);
        return (len > 0) ? len : 0;
      } else {
        int len = snprintf(out, MOUSE_SEQ_MAX, "\x1B[<%d;%d;%dM", cb, px, py);
        return (len > 0) ? len : 0;
      }
    } else {
      if (is_release) {
        int len = snprintf(out, MOUSE_SEQ_MAX, "\x1B[<%d;%d;%dm", cb, n_col, n_row);
        return (len > 0) ? len : 0;
      } else {
        int len = snprintf(out, MOUSE_SEQ_MAX, "\x1B[<%d;%d;%dM", cb, n_col, n_row);
        return (len > 0) ? len : 0;
      }
    }
  }

  if (mouse_encoding == MOUSE_ENC_URXVT) {
    // urxvt: ESC [ cb ; col ; row M
    int len = snprintf(out, MOUSE_SEQ_MAX, "\x1B[%d;%d;%dM", cb, n_col, n_row);
    return (len > 0) ? len : 0;
  }

  return 0;
}

/**
 * @brief Build a scroll wheel escape sequence.
 */
static int build_scroll_seq(char out[MOUSE_SEQ_MAX], double yoffset,
                            int col, int row, int mods) {
  out[0] = '\0';

  // Scroll button codes:
  //   yoffset > 0 = scroll up  (cb = 64, button 4)
  //   yoffset < 0 = scroll down (cb = 65, button 5)
  int cb = (yoffset > 0) ? 64 : 65;

  // Modifier flags
  if (mods & GLFW_MOD_SHIFT)   cb |= 4;
  if (mods & GLFW_MOD_ALT)     cb |= 8;
  if (mods & GLFW_MOD_CONTROL) cb |= 16;

  int n_col = col;
  int n_row = row;

  if (mouse_enc_is_offset()) {
    if (n_col > 223) n_col = 223;
    if (n_row > 223) n_row = 223;
    int len = snprintf(out, MOUSE_SEQ_MAX, "\x1B[M%c%c%c",
                       (char)(cb + 32), (char)(n_col + 32), (char)(n_row + 32));
    return (len > 0) ? len : 0;
  }

  if (mouse_encoding == MOUSE_ENC_SGR) {
    int len = snprintf(out, MOUSE_SEQ_MAX, "\x1B[<%d;%d;%dM", cb, n_col, n_row);
    return (len > 0) ? len : 0;
  }

  if (mouse_encoding == MOUSE_ENC_SGR_PIX) {
    int px = (int)((double)(col - 1) * mouse_char_w + mouse_char_w / 2.0);
    int py = (int)((double)(row - 1) * mouse_char_h + mouse_char_h / 2.0);
    int len = snprintf(out, MOUSE_SEQ_MAX, "\x1B[<%d;%d;%dM", cb, px, py);
    return (len > 0) ? len : 0;
  }

  if (mouse_encoding == MOUSE_ENC_URXVT) {
    int len = snprintf(out, MOUSE_SEQ_MAX, "\x1B[%d;%d;%dM", cb, n_col, n_row);
    return (len > 0) ? len : 0;
  }

  return 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void mouse_init(GLFWwindow *window,
                int term_cols, int term_rows,
                int win_padding, float char_w, float char_h) {
  mouse_window      = window;
  mouse_tracking    = MOUSE_TRACK_OFF;
  mouse_encoding    = MOUSE_ENC_DEFAULT;
  mouse_term_cols   = term_cols;
  mouse_term_rows   = term_rows;
  mouse_padding     = win_padding;
  mouse_char_w      = (double)char_w;
  mouse_char_h      = (double)char_h;
  mouse_qcount      = 0;
  mouse_last_btn    = -1;

  for (int i = 0; i < 3; i++)
    mouse_btn_state[i] = false;

  // Install GLFW callbacks
  glfwSetMouseButtonCallback(window, mouse_button_callback);
  glfwSetCursorPosCallback(window, mouse_move_callback);
  glfwSetScrollCallback(window, mouse_scroll_callback);
  glfwSetCursorEnterCallback(window, mouse_enter_callback);
}

void mouse_update_geometry(int term_cols, int term_rows,
                           int win_padding, float char_w, float char_h) {
  mouse_term_cols = term_cols;
  mouse_term_rows = term_rows;
  mouse_padding   = win_padding;
  mouse_char_w    = (double)char_w;
  mouse_char_h    = (double)char_h;
}

bool mouse_set_tracking(mouse_tracking_t tracking) {
  bool changed = (mouse_tracking != tracking);
  mouse_tracking = tracking;
  return changed;
}

void mouse_set_encoding(mouse_encoding_t encoding) {
  mouse_encoding = encoding;
}

/**
 * @brief Process all queued mouse events and write sequences to the PTY.
 *
 * Handles button press/release, motion (with drag tracking), wheel events,
 * and focus enter/leave.  Respects the active mouse tracking mode.
 *
 * @param fd  PTY master file descriptor.
 * @return    Total bytes written, or -1 on write error.
 */
int process_mouse_input(int fd) {
  if (mouse_tracking == MOUSE_TRACK_OFF)
    return 0;

  int total = 0;
  char seq[MOUSE_SEQ_MAX];

  for (int i = 0; i < mouse_qcount; i++) {
    mouse_qevent_t *ev = &mouse_queue[i];

    int col = 1, row = 1;
    bool pos_valid = false;

    switch (ev->type) {
    case 0: { // Button event
      pos_valid = pixel_to_cell(ev->x, ev->y, &col, &row);
      if (!pos_valid) break;

      bool is_release = (ev->action == GLFW_RELEASE);
      int cb = encode_button(ev->button, ev->action, ev->mods, false);
      int len = build_mouse_seq(seq, cb, col, row, is_release, false);
      if (len > 0) {
        if (mouse_write_str(fd, seq) < 0) return -1;
        total += len;
      }
      break;
    }

    case 1: { // Motion event
      pos_valid = pixel_to_cell(ev->x, ev->y, &col, &row);
      if (!pos_valid) break;

      bool any_btn = mouse_btn_state[0] || mouse_btn_state[1] || mouse_btn_state[2];
      int active_btn = -1;

      if (mouse_btn_state[0]) active_btn = GLFW_MOUSE_BUTTON_LEFT;
      else if (mouse_btn_state[2]) active_btn = GLFW_MOUSE_BUTTON_RIGHT;
      else if (mouse_btn_state[1]) active_btn = GLFW_MOUSE_BUTTON_MIDDLE;

      if (any_btn && active_btn >= 0 &&
          mouse_tracking >= MOUSE_TRACK_BTN) {
        // Button event tracking (?1002) or any-event (?1003):
        // Forward motion while dragging.
        int cb = encode_button(active_btn, GLFW_PRESS, 0, true);
        int len = build_mouse_seq(seq, cb, col, row, false, true);
        if (len > 0) {
          if (mouse_write_str(fd, seq) < 0) return -1;
          total += len;
        }
      } else if (!any_btn && mouse_tracking == MOUSE_TRACK_ANY) {
        // Any-event mode (?1003): forward all motion, even without button.
        int len = build_mouse_seq(seq, 0, col, row, false, true);
        if (len > 0) {
          if (mouse_write_str(fd, seq) < 0) return -1;
          total += len;
        }
      }
      break;
    }

    case 2: { // Scroll event
      // Use current cursor position if available
      double cx, cy;
      glfwGetCursorPos(mouse_window, &cx, &cy);
      pixel_to_cell(cx, cy, &col, &row);

      int len = build_scroll_seq(seq, ev->sy, col, row, 0);
      if (len > 0) {
        if (mouse_write_str(fd, seq) < 0) return -1;
        total += len;
      }
      break;
    }

    case 3: // Focus enter/leave (not typically reported)
    default:
      break;
    }
  }

  mouse_qcount = 0;
  return total;
}

bool mouse_is_enabled(void) {
  return mouse_tracking != MOUSE_TRACK_OFF;
}

double mouse_consume_scroll(void) {
  double total = 0.0;
  for (int i = 0; i < mouse_qcount; i++) {
    if (mouse_queue[i].type == 2) {
      total += mouse_queue[i].sy;
    }
  }
  mouse_qcount = 0;
  return total;
}
