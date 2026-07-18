#include "input.h"

#include <errno.h>
#include <raylib.h>
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
// Poll raylib and write all pending key events to fd
// ---------------------------------------------------------------------------

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
  int safety = 256;
  int c;
  while ((c = GetCharPressed()) > 0 && safety-- > 0) {
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
      bool alt_held = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
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

  int key;
  while ((key = GetKeyPressed()) != 0) {
    // Check modifiers at the time of this poll.
    shift_held = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    ctrl_held  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    alt_held   = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);

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

  return total_written;
}
