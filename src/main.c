#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <GLFW/glfw3.h>
#if defined(HAS_X11)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif
#include <GL/glcorearb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "config.h"
#include "parser.h"
#include "font.h"
#include "input.h"
#include "scrollback.h"
#include "screen.h"
#include "gl_renderer.h"
#include "clipboard.h"

// Terminal geometry – computed from window size and font metrics at startup.
static int WIN_PADDING = 10;
static int WIN_WIDTH  = 1280;
static int WIN_HEIGHT = 800;

/**
 * @file main.c
 * @brief Terminal emulator entry point and main loop.
 *
 * Spawns a child shell via forkpty, parses its output with the ANSI
 * escape sequence state machine (parser.c), stores characters together
 * with SGR colour attributes in a screen_buf_t grid, and renders each
 * cell with its individual colours using raylib's font subsystem.
 */

/** @brief Top-level terminal emulator state. */
typedef struct {
  screen_buf_t  screen;            /**< Active screen grid with SGR attributes. */
  screen_buf_t  alt_screen;        /**< Alternate screen buffer (lazy init). */
  bool          use_alt_screen;    /**< True when alternate screen is active. */
  int           cx;                /**< Cursor column (0-based). */
  int           cy;                /**< Cursor row (0-based). */
  int           saved_cx;          /**< Saved cursor column (DECSC/DECRC). */
  int           saved_cy;          /**< Saved cursor row (DECSC/DECRC). */
  int           saved_cx_1048;     /**< Saved cursor column (DEC ?1048). */
  int           saved_cy_1048;     /**< Saved cursor row (DEC ?1048). */

  uint8_t       cur_fg[3];         /**< Current SGR foreground colour (RGB). */
  uint8_t       cur_bg[3];         /**< Current SGR background colour (RGB). */
  bool          cur_bold;          /**< Current bold attribute state. */
  bool          cur_underline;     /**< Current underline attribute state. */

  parser_t      parser;            /**< ANSI escape sequence parser instance. */
  scrollback_t *scrollback;        /**< Scrollback ring buffer for off-screen lines. */
  int           scroll_offset;     /**< Scrollback viewport offset (0 = normal, > 0 = scrolled back). */
  font_handle_t *font;             /**< Font rendering handle. */
  int           win_width;         /**< Current window width in pixels. */
  int           win_height;        /**< Current window height in pixels. */

  int           utf8_codepoint;    /**< Partially accumulated UTF-8 codepoint. */
  int           utf8_remaining;    /**< UTF-8 continuation bytes still expected. */

  // Selection state (mouse-based text selection)
  struct {
    bool active;          /**< User is currently dragging to select. */
    bool has_selection;   /**< A completed selection exists. */
    int  start_row;       /**< 0-based start row. */
    int  start_col;       /**< 0-based start column. */
    int  end_row;         /**< 0-based end row. */
    int  end_col;         /**< 0-based end column. */
  } selection;

  /** Bracketed paste mode (DECSET ?2004). */
  bool bracketed_paste;
} terminal_t;

// ---------------------------------------------------------------------------
// UTF-8 decoder – stateful byte-at-a-time decoder.
// ---------------------------------------------------------------------------

/**
 * @brief Decode one byte of a UTF-8 sequence.
 *
 * Call for each byte in sequence.  Return values:
 *   - -2: continuation byte accepted, codepoint still incomplete
 *   - -1: invalid byte sequence (decoder reset)
 *   -  0: start byte accepted, need more continuation bytes
 *   - >0: complete codepoint decoded (returned value)
 *
 * @param t  Terminal state (holds decoder state in utf8_* fields).
 * @param b  The byte to decode.
 * @return   See detailed description above.
 */

static int utf8_decode_byte(terminal_t *t, uint8_t b) {
  if (b < 0x80) {
    // Single-byte ASCII – reset state and return immediately.
    t->utf8_remaining = 0;
    t->utf8_codepoint = 0;
    return b;
  }

  if (t->utf8_remaining > 0) {
    // Continuation byte (10xxxxxx)
    if ((b & 0xC0) != 0x80) {
      // Invalid continuation byte – reset.
      t->utf8_remaining = 0;
      t->utf8_codepoint = 0;
      return -1;
    }
    t->utf8_codepoint = (t->utf8_codepoint << 6) | (b & 0x3F);
    t->utf8_remaining--;
    if (t->utf8_remaining == 0) {
      int cp = t->utf8_codepoint;
      t->utf8_codepoint = 0;
      return cp;
    }
    return -2; // continuation byte accepted, more needed
  }

  // Start byte of a multi-byte sequence
  t->utf8_codepoint = 0;
  t->utf8_remaining = 0;

  if ((b & 0xE0) == 0xC0) {
    // 2-byte sequence: 110xxxxx 10xxxxxx
    t->utf8_codepoint = b & 0x1F;
    t->utf8_remaining = 1;
  } else if ((b & 0xF0) == 0xE0) {
    // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
    t->utf8_codepoint = b & 0x0F;
    t->utf8_remaining = 2;
  } else if ((b & 0xF8) == 0xF0) {
    // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    t->utf8_codepoint = b & 0x07;
    t->utf8_remaining = 3;
  } else {
    // Invalid start byte
    return -1;
  }
  return 0; // need more bytes
}

/**
 * @brief Reset the UTF-8 decoder state.
 *
 * Must be called after any escape sequence, C0 control, or other
 * non-printable event to discard any partially accumulated codepoint.
 *
 * @param t  Terminal state.
 */
static void utf8_reset(terminal_t *t) {
  t->utf8_remaining = 0;
  t->utf8_codepoint = 0;
}

// ---------------------------------------------------------------------------
// Colour tables for ANSI SGR codes
// ---------------------------------------------------------------------------

/** @brief Standard ANSI colours for SGR codes 30-37 (fg) and 40-47 (bg). */
static const uint8_t ansi_std[8][3] = {
  {  0,   0,   0},  // 0 black
  {205,  49,  49},  // 1 red
  { 13, 188, 121},  // 2 green
  {229, 229,  16},  // 3 yellow
  { 36, 114, 200},  // 4 blue
  {188,  63, 188},  // 5 magenta
  { 17, 168, 205},  // 6 cyan
  {229, 229, 229},  // 7 white
};

/** @brief Bright ANSI colours for SGR codes 90-97 (fg) and 100-107 (bg). */
static const uint8_t ansi_bright[8][3] = {
  {128, 128, 128},  // 0 bright black (grey)
  {255,  85,  85},  // 1 bright red
  { 80, 255, 123},  // 2 bright green
  {255, 255,  85},  // 3 bright yellow
  {100, 150, 255},  // 4 bright blue
  {255,  85, 255},  // 5 bright magenta
  { 85, 255, 255},  // 6 bright cyan
  {255, 255, 255},  // 7 bright white
};

// Default colours (used when SGR 0 is sent) – initialised from config at startup.
static uint8_t default_fg[3] = {220, 220, 220};
static uint8_t default_bg[3] = {  0,   0,   0};

// ---------------------------------------------------------------------------
// Scroll operations
// ---------------------------------------------------------------------------

/// @brief Scroll the screen content up by `count` rows.
///
/// The top `count` rows are pushed to the scrollback buffer and the
/// bottom rows are filled with space characters using default colours.
///
/// @param t      Terminal state.
/// @param count  Number of rows to scroll up.
static void scroll_up(terminal_t *t, int count) {
  int sb_cols = t->scrollback ? scrollback_cols(t->scrollback) : t->screen.cols;
  int screen_cols = t->screen.cols;
  int copy_cols = screen_cols < sb_cols ? screen_cols : sb_cols;
  int *line_buf = malloc((size_t)sb_cols * sizeof(int));
  if (!line_buf) return;
  for (int n = 0; n < count; n++) {
    // Copy the top row's codepoints, padding with spaces if screen is narrower
    for (int c = 0; c < copy_cols; c++)
      line_buf[c] = t->screen.cells[c].ch;
    for (int c = copy_cols; c < sb_cols; c++)
      line_buf[c] = ' ';
    scrollback_push(t->scrollback, line_buf);

    // Shift all rows up by one
    memmove(t->screen.cells,
            t->screen.cells + screen_cols,
             ((size_t)(t->screen.rows - 1) * (size_t)screen_cols * sizeof(screen_cell_t)));
    // Clear the new bottom row
    for (int c = 0; c < screen_cols; c++) {
      screen_cell_t *cell = &t->screen.cells[(t->screen.rows - 1) * screen_cols + c];
      cell->ch = ' ';
      cell->fg[0] = default_fg[0]; cell->fg[1] = default_fg[1]; cell->fg[2] = default_fg[2];
      cell->bg[0] = default_bg[0]; cell->bg[1] = default_bg[1]; cell->bg[2] = default_bg[2];
      cell->bold = cell->italic = cell->underline = cell->blink = 0;
    }
  }
  free(line_buf);
}

/// @brief Scroll the screen content down by `count` rows (reverse scroll).
///
/// The bottom rows are scrolled down and the top rows are filled with
/// spaces using default colours.
///
/// @param t      Terminal state.
/// @param count  Number of rows to scroll down.
static void scroll_down(terminal_t *t, int count) {
  int cols = t->screen.cols;
  for (int n = 0; n < count; n++) {
    // Shift all rows down by one
    memmove(t->screen.cells + cols,
            t->screen.cells,
             ((size_t)(t->screen.rows - 1) * (size_t)cols * sizeof(screen_cell_t)));
    // Clear the new top row
    for (int c = 0; c < cols; c++) {
      screen_cell_t *cell = &t->screen.cells[c];
      cell->ch = ' ';
      cell->fg[0] = default_fg[0]; cell->fg[1] = default_fg[1]; cell->fg[2] = default_fg[2];
      cell->bg[0] = default_bg[0]; cell->bg[1] = default_bg[1]; cell->bg[2] = default_bg[2];
      cell->bold = cell->italic = cell->underline = cell->blink = 0;
    }
  }
}

// ---------------------------------------------------------------------------
// Scrollback viewport handling
// ---------------------------------------------------------------------------

/// @brief Handle scrollback navigation keys.
///
/// When the viewport is scrolled back, the user can navigate with:
///   - Page Up:    scroll up by one page (screen height)
///   - Page Down:  scroll down by one page
///   - Shift+Up:   scroll up by one line
///   - Shift+Down: scroll down by one line
///
/// Page Up / Page Down are reserved in input.c (not forwarded to the PTY)
/// so they are safe to use here.  Shift+Up / Shift+Down are also reserved.
///
/// @param t  Terminal state.
static void handle_scrollback_keys(terminal_t *t) {
  int max_offset = scrollback_count(t->scrollback);

  if (input_key_pressed(KEY_PAGE_UP)) {
    t->scroll_offset += t->screen.rows;
    if (t->scroll_offset > max_offset)
      t->scroll_offset = max_offset;
  }
  if (input_key_pressed(KEY_PAGE_DOWN)) {
    t->scroll_offset -= t->screen.rows;
    if (t->scroll_offset < 0)
      t->scroll_offset = 0;
  }

  bool shift_held = input_key_down(KEY_LEFT_SHIFT) || input_key_down(KEY_RIGHT_SHIFT);
  if (shift_held) {
    if (input_key_pressed(KEY_UP)) {
      t->scroll_offset++;
      if (t->scroll_offset > max_offset)
        t->scroll_offset = max_offset;
    }
    if (input_key_pressed(KEY_DOWN)) {
      t->scroll_offset--;
      if (t->scroll_offset < 0)
        t->scroll_offset = 0;
    }
  }
}

// ---------------------------------------------------------------------------
// Screen helpers
// ---------------------------------------------------------------------------

/// @brief Clear the entire screen and scrollback, return cursor to home.
///
/// Resets all cells to space with default colours and clears the
/// scrollback history.  Cursor is moved to (0, 0).
///
/// @param t  Terminal state.
static void clear_screen(terminal_t *t) {
  for (int r = 0; r < t->screen.rows; r++)
    screen_buf_clear_row(&t->screen, r, true);
  scrollback_clear(t->scrollback);
  t->scroll_offset = 0;
  t->cx = 0;
  t->cy = 0;
}

// ---------------------------------------------------------------------------
// Parser callbacks – invoked by parser.c state machine.
// ---------------------------------------------------------------------------

/// @brief Handle a printable character or UTF-8 byte from the child shell.
///
/// For ASCII bytes (0x20–0x7E) this writes the character directly into
/// the current cursor cell.  For bytes >= 0x80 it accumulates multi-byte
/// UTF-8 sequences via the terminal's utf8 decoder and only writes a
/// complete decoded codepoint to the cell once all continuation bytes
/// have been received.  Advances the cursor; scrolls up if the cursor
/// moves past the last row.
///
/// @param ch   Character byte (may be part of a multi-byte UTF-8 seq).
/// @param ctx  Pointer to terminal_t (opaque context).
static void on_print(uint8_t ch, void *ctx) {
  terminal_t *t = (terminal_t *)ctx;

  // Single-byte ASCII fast path
  if (ch < 0x80) {
    // Reset any in-progress UTF-8 sequence
    utf8_reset(t);

    if (t->cx >= t->screen.cols) {
      t->cx = 0;
      t->cy++;
    }
    if (t->cy >= t->screen.rows) {
      scroll_up(t, 1);
      t->cy = t->screen.rows - 1;
    }

    screen_cell_t *cell = screen_buf_cell(&t->screen, t->cy, t->cx);
    if (cell) {
      cell->ch = ch;
      cell->fg[0] = t->cur_fg[0]; cell->fg[1] = t->cur_fg[1]; cell->fg[2] = t->cur_fg[2];
      cell->bg[0] = t->cur_bg[0]; cell->bg[1] = t->cur_bg[1]; cell->bg[2] = t->cur_bg[2];
      cell->bold = t->cur_bold;
      cell->underline = t->cur_underline;
    }
    t->cx++;
    return;
  }

  // Multi-byte UTF-8: feed the byte into the decoder
  int result = utf8_decode_byte(t, ch);
  if (result > 0) {
    // A complete codepoint was decoded – write it into the cell
    if (t->cx >= t->screen.cols) {
      t->cx = 0;
      t->cy++;
    }
    if (t->cy >= t->screen.rows) {
      scroll_up(t, 1);
      t->cy = t->screen.rows - 1;
    }

    screen_cell_t *cell = screen_buf_cell(&t->screen, t->cy, t->cx);
    if (cell) {
      cell->ch = result;
      cell->fg[0] = t->cur_fg[0]; cell->fg[1] = t->cur_fg[1]; cell->fg[2] = t->cur_fg[2];
      cell->bg[0] = t->cur_bg[0]; cell->bg[1] = t->cur_bg[1]; cell->bg[2] = t->cur_bg[2];
      cell->bold = t->cur_bold;
      cell->underline = t->cur_underline;
    }
    t->cx++;
  }
  // If result == 0 (need more bytes) or result < 0 (invalid), we just
  // accumulate or ignore – no cell write yet.
}

/// @brief Handle a C0 or C1 control character.
///
/// Processes LF, CR, BS, HT, VT, FF by moving the cursor or scrolling.
///
/// C0 controls (< 0x20) always terminate an in-progress UTF-8 multi-byte
/// sequence because they cannot be UTF-8 continuation bytes.
///
/// C1 controls (0x80–0x9F) can also be valid UTF-8 continuation bytes
/// (10xxxxxx pattern).  If we are in the middle of decoding a multi-byte
/// UTF-8 character, the byte is fed to the UTF-8 decoder instead of being
/// processed as a control.  This handles NERD Font codepoints whose second
/// byte falls in the 0x84–0x8F range (e.g. U+E100–U+E1FF, U+E240–U+E2FF).
///
/// @param c0   Control character byte (may be 0x00–0x1F or 0x80–0x9F).
/// @param ctx  Pointer to terminal_t.
static void on_execute(char c0, void *ctx) {
  terminal_t *t = (terminal_t *)ctx;
  unsigned char ub = (unsigned char)c0;

  // C0 controls (0x00–0x1F) always reset UTF-8.
  if (ub < 0x20) {
    utf8_reset(t);
  } else if (ub >= 0x80) {
    // C1 control bytes (0x80–0x9F) may overlap with UTF-8 continuation
    // bytes.  If we are in the middle of a multi-byte sequence, feed
    // this byte to the UTF-8 decoder instead of executing the control.
    if (t->utf8_remaining > 0) {
      int result = utf8_decode_byte(t, ub);
      if (result > 0) {
        // A complete codepoint was decoded — write it into the cell
        if (t->cx >= t->screen.cols) {
          t->cx = 0;
          t->cy++;
        }
        if (t->cy >= t->screen.rows) {
          scroll_up(t, 1);
          t->cy = t->screen.rows - 1;
        }
        screen_cell_t *cell = screen_buf_cell(&t->screen, t->cy, t->cx);
        if (cell) {
          cell->ch = result;
          cell->fg[0] = t->cur_fg[0]; cell->fg[1] = t->cur_fg[1]; cell->fg[2] = t->cur_fg[2];
          cell->bg[0] = t->cur_bg[0]; cell->bg[1] = t->cur_bg[1]; cell->bg[2] = t->cur_bg[2];
          cell->bold = t->cur_bold;
          cell->underline = t->cur_underline;
        }
        t->cx++;
      }
      // If result <= 0 (need more or invalid), we just accumulate or ignore.
      return; // Don't process as control
    }
    // If not in a UTF-8 sequence, treat as a normal control. Reset state.
    utf8_reset(t);
  }

  switch (c0) {
  default:
    break;
  case '\n':
    t->cy++;
    if (t->cy >= t->screen.rows) {
      scroll_up(t, 1);
      t->cy = t->screen.rows - 1;
    }
    break;
  case '\r': t->cx = 0; break;
  case '\b':
    if (t->cx > 0) t->cx--;
    break;
  case '\t': {
    int stop = ((t->cx / 8) + 1) * 8;
    t->cx = (stop < t->screen.cols) ? stop : t->screen.cols - 1;
    break;
  }
  case 0x0B: case 0x0C:  // VT, FF
    t->cy++;
    if (t->cy >= t->screen.rows) {
      scroll_up(t, 1);
      t->cy = t->screen.rows - 1;
    }
    break;
  }
}

// ---------------------------------------------------------------------------
// 256-colour palette resolver
// ---------------------------------------------------------------------------

/// @brief Resolve an 8-bit colour index (0–255) into RGB.
///
/// Palette layout (standard xterm):
///   -   0– 7:  standard ANSI colours
///   -   8–15:  bright ANSI colours
///   -  16–231: 6×6×6 RGB cube (216 colours)
///   - 232–255: 24-step greyscale ramp
///
/// @param out  Output RGB array (3 bytes).
/// @param idx  Colour index (clamped to 0–255).
static void resolve_256(uint8_t out[3], int idx) {
  if (idx < 0) idx = 0;
  if (idx > 255) idx = 255;

  if (idx < 16) {
    if (idx < 8) {
      out[0] = ansi_std[idx][0];
      out[1] = ansi_std[idx][1];
      out[2] = ansi_std[idx][2];
    } else {
      out[0] = ansi_bright[idx - 8][0];
      out[1] = ansi_bright[idx - 8][1];
      out[2] = ansi_bright[idx - 8][2];
    }
  } else if (idx < 232) {
    int n = idx - 16;
    int r = n / 36;
    int g = (n % 36) / 6;
    int b = n % 6;
    // 6×6×6 cube levels: 0, 95, 135, 175, 215, 255
    static const uint8_t cube_level[6] = {0, 95, 135, 175, 215, 255};
    out[0] = cube_level[r];
    out[1] = cube_level[g];
    out[2] = cube_level[b];
  } else {
    // Greyscale ramp: 8, 18, 28, … 238
    int grey = 8 + (idx - 232) * 10;
    out[0] = out[1] = out[2] = (uint8_t)grey;
  }
}

// ---------------------------------------------------------------------------
// SGR colour handling
// ---------------------------------------------------------------------------

/// @brief Reset all SGR attributes to their defaults.
///
/// Foreground → {220,220,220}, background → {0,0,0} (transparent),
/// bold and underline → false.
///
/// @param t  Terminal state.
static void reset_sgr(terminal_t *t) {
  t->cur_fg[0] = default_fg[0]; t->cur_fg[1] = default_fg[1]; t->cur_fg[2] = default_fg[2];
  t->cur_bg[0] = default_bg[0]; t->cur_bg[1] = default_bg[1]; t->cur_bg[2] = default_bg[2];
  t->cur_bold = false;
  t->cur_underline = false;
}

/// @brief Set foreground to a standard ANSI colour (codes 30–37).
/// @param t    Terminal state.
/// @param idx  0-based index into ansi_std[] (0 = black, 7 = white).
static void set_fg(terminal_t *t, int idx) {
  if (idx >= 0 && idx < 8) {
    t->cur_fg[0] = ansi_std[idx][0];
    t->cur_fg[1] = ansi_std[idx][1];
    t->cur_fg[2] = ansi_std[idx][2];
  }
}

/// @brief Set background to a standard ANSI colour (codes 40–47).
/// @param t    Terminal state.
/// @param idx  0-based index into ansi_std[].
static void set_bg(terminal_t *t, int idx) {
  if (idx >= 0 && idx < 8) {
    t->cur_bg[0] = ansi_std[idx][0];
    t->cur_bg[1] = ansi_std[idx][1];
    t->cur_bg[2] = ansi_std[idx][2];
  }
}

/// @brief Set foreground to a bright ANSI colour (codes 90–97).
/// @param t    Terminal state.
/// @param idx  0-based index into ansi_bright[].
static void set_bright_fg(terminal_t *t, int idx) {
  if (idx >= 0 && idx < 8) {
    t->cur_fg[0] = ansi_bright[idx][0];
    t->cur_fg[1] = ansi_bright[idx][1];
    t->cur_fg[2] = ansi_bright[idx][2];
  }
}

/// @brief Set background to a bright ANSI colour (codes 100–107).
/// @param t    Terminal state.
/// @param idx  0-based index into ansi_bright[].
static void set_bright_bg(terminal_t *t, int idx) {
  if (idx >= 0 && idx < 8) {
    t->cur_bg[0] = ansi_bright[idx][0];
    t->cur_bg[1] = ansi_bright[idx][1];
    t->cur_bg[2] = ansi_bright[idx][2];
  }
}

// ---------------------------------------------------------------------------
// Alternate screen buffer management
// ---------------------------------------------------------------------------

/// @brief Switch to the alternate screen buffer.
///
/// If the alternate screen hasn't been allocated yet, it is created with
/// the same dimensions as the current screen.  The active screen and
/// alternate screen are swapped (so the current screen content is saved),
/// the new screen is cleared, and the cursor is reset to (0,0).
/// Scrollback mode is also reset.
///
/// @param t  Terminal state.
static void switch_to_alt_screen(terminal_t *t) {
  if (t->use_alt_screen) return;
  // Lazily allocate the alt screen buffer
  if (!t->alt_screen.cells) {
    screen_buf_t *sb = screen_buf_new(t->screen.rows, t->screen.cols);
    if (!sb) return;
    t->alt_screen = *sb;
    free(sb);
  }
  // Swap the buffers: screen becomes the alt buffer, alt becomes primary
  screen_buf_t tmp = t->screen;
  t->screen = t->alt_screen;
  t->alt_screen = tmp;
  t->use_alt_screen = true;
  // Clear the new active screen (the alt buffer)
  for (int r = 0; r < t->screen.rows; r++)
    screen_buf_clear_row(&t->screen, r, true);
  t->cx = 0;
  t->cy = 0;
  t->scroll_offset = 0;
}

/// @brief Switch back to the primary screen buffer.
///
/// Swaps the active and alternate buffers back, restoring the original
/// screen content.  Cursor is reset to (0,0) and scrollback mode exits.
///
/// @param t  Terminal state.
static void switch_to_primary_screen(terminal_t *t) {
  if (!t->use_alt_screen) return;
  screen_buf_t tmp = t->screen;
  t->screen = t->alt_screen;
  t->alt_screen = tmp;
  t->use_alt_screen = false;
  t->scroll_offset = 0;
}

// ---------------------------------------------------------------------------
// CSI handler – processes Control Sequence Introducer (ESC [ ... ) sequences.
// ---------------------------------------------------------------------------

/// @brief Handle a complete CSI sequence.
///
/// Dispatches on the final byte to implement cursor movement (A/B/C/D/H/f),
/// erase operations (J/K), save/restore cursor (s/u), SGR colour and
/// attribute changes (m), and scroll (S/T).
///
/// SGR (final 'm') handles:
///   - Standard colours: 30–37 (fg), 40–47 (bg)
///   - Bright colours:   90–97 (fg), 100–107 (bg)
///   - Extended colours: 38 (fg), 48 (bg), 58 (underline)
///     - 38;5;N  / 38:5:N   → 256-colour indexed
///     - 38;2;R;G;B / 38:2:R:G:B → 24-bit truecolor
///   - Default resets: 39 (fg), 49 (bg)
///   - Attributes: 0 (reset), 1 (bold), 4 (underline), 22/24 (off)
///
/// @param params             Numeric parameters (-1 = omitted).
/// @param num_params         Number of valid entries in @p params.
/// @param intermediates      Intermediate bytes (e.g. '?' for DEC private).
/// @param num_intermediates  Number of intermediate bytes.
/// @param final              Final byte of the CSI sequence (0x40–0x7E).
/// @param ctx                Pointer to terminal_t.
static void on_csi(int params[16], int num_params,
                   char intermediates[2], int num_intermediates,
                   char final, void *ctx) {
  terminal_t *t = (terminal_t *)ctx;
  // An escape/CSI sequence breaks any in-progress UTF-8 multi-byte sequence
  utf8_reset(t);

#define PARAM(n) ((n) < num_params && params[(n)] >= 0 ? params[(n)] : -1)

  // ---- DEC private SET/RESET (intermediate '?' → h/l) ----
  // Handles DECSET (h) and DECRST (l) for modes like:
  //   ?2004h/l – bracketed paste mode
  //   ?1047h/l – alternate screen buffer
  //   ?1048h/l – save/restore cursor
  //   ?1049h/l – save cursor + alternate screen / restore cursor + primary screen
  if (num_intermediates > 0 && intermediates[0] == '?') {
    if (final == 'h' || final == 'l') {
      bool set = (final == 'h');
      int p = PARAM(0);
      switch (p) {
      case 2004:
        t->bracketed_paste = set;
        break;
      case 1047:
        if (set)
          switch_to_alt_screen(t);
        else
          switch_to_primary_screen(t);
        break;
      case 1048:
        if (set) {
          t->saved_cx_1048 = t->cx;
          t->saved_cy_1048 = t->cy;
        } else {
          t->cx = t->saved_cx_1048;
          t->cy = t->saved_cy_1048;
        }
        break;
      case 1049:
        if (set) {
          t->saved_cx_1048 = t->cx;
          t->saved_cy_1048 = t->cy;
          switch_to_alt_screen(t);
        } else {
          switch_to_primary_screen(t);
          t->cx = t->saved_cx_1048;
          t->cy = t->saved_cy_1048;
        }
        break;
      default:
        break;
      }
      // Future: handle ?25 (cursor vis), ?1000/1002/1003 (mouse tracking)
    }
    return; // handled
  }

  switch (final) {
  default:
    break;
  // ---- Cursor movement ----
  case 'A': {
    int n = PARAM(0); if (n < 0) n = 1;
    t->cy -= n; if (t->cy < 0) t->cy = 0;
    break;
  }
  case 'B': {
    int n = PARAM(0); if (n < 0) n = 1;
    t->cy += n; if (t->cy >= t->screen.rows) t->cy = t->screen.rows - 1;
    break;
  }
  case 'C': {
    int n = PARAM(0); if (n < 0) n = 1;
    t->cx += n; if (t->cx >= t->screen.cols) t->cx = t->screen.cols - 1;
    break;
  }
  case 'D': {
    int n = PARAM(0); if (n < 0) n = 1;
    t->cx -= n; if (t->cx < 0) t->cx = 0;
    break;
  }
  case 'H': case 'f': {
    int row = PARAM(0); if (row < 0) row = 1; row--;
    int col = PARAM(1); if (col < 0) col = 1; col--;
    if (row < 0) row = 0; if (row >= t->screen.rows) row = t->screen.rows - 1;
    if (col < 0) col = 0; if (col >= t->screen.cols) col = t->screen.cols - 1;
    t->cy = row; t->cx = col;
    break;
  }

  // ---- Erase ----
  case 'J': {
    int mode = PARAM(0); if (mode < 0) mode = 0;
    screen_buf_erase_display(&t->screen, t->cy, t->cx, mode, true);
    break;
  }
  case 'K': {
    int mode = PARAM(0); if (mode < 0) mode = 0;
    screen_buf_erase_line(&t->screen, t->cy, t->cx, mode, true);
    break;
  }

  // ---- Save / Restore cursor (DEC private) ----
  case 's': t->saved_cx = t->cx; t->saved_cy = t->cy; break;
  case 'u': t->cx = t->saved_cx; t->cy = t->saved_cy; break;

  // ---- SGR (Select Graphic Rendition) ----
  case 'm': {
    if (num_params == 0) {
      reset_sgr(t);
      break;
    }
    for (int i = 0; i < num_params; i++) {
      int p = params[i];
      if (p < 0) p = 0;

      if (p == 0) {
        reset_sgr(t);
      } else if (p == 1) {
        t->cur_bold = true;
      } else if (p == 22) {
        t->cur_bold = false;
      } else if (p == 4) {
        t->cur_underline = true;
      } else if (p == 24) {
        t->cur_underline = false;

      // ---- Default colour resets ----
      } else if (p == 39) {
        t->cur_fg[0] = default_fg[0]; t->cur_fg[1] = default_fg[1]; t->cur_fg[2] = default_fg[2];
      } else if (p == 49) {
        t->cur_bg[0] = default_bg[0]; t->cur_bg[1] = default_bg[1]; t->cur_bg[2] = default_bg[2];

      // ---- Standard foreground / background ----
      } else if (p >= 30 && p <= 37) {
        set_fg(t, p - 30);
      } else if (p >= 40 && p <= 47) {
        set_bg(t, p - 40);
      } else if (p >= 90 && p <= 97) {
        set_bright_fg(t, p - 90);
      } else if (p >= 100 && p <= 107) {
        set_bright_bg(t, p - 100);

      // ---- Extended foreground colour: 38;5;N  or  38;2;R;G;B ----
      } else if (p == 38) {
        if (i + 1 < num_params) {
          int mode = params[i + 1];
          if (mode == 5 && i + 2 < num_params) {
            // 256-colour indexed
            int idx = params[i + 2];
            if (idx >= 0) resolve_256(t->cur_fg, idx);
            i += 2;
          } else if (mode == 2 && i + 4 < num_params) {
            // 24-bit RGB
            int r = params[i + 2];
            int g = params[i + 3];
            int b = params[i + 4];
            if (r >= 0) t->cur_fg[0] = (uint8_t)(r & 0xFF);
            if (g >= 0) t->cur_fg[1] = (uint8_t)(g & 0xFF);
            if (b >= 0) t->cur_fg[2] = (uint8_t)(b & 0xFF);
            i += 4;
          }
          // If mode is unrecognised we silently skip
        }

      // ---- Extended background colour: 48;5;N  or  48;2;R;G;B ----
      } else if (p == 48) {
        if (i + 1 < num_params) {
          int mode = params[i + 1];
          if (mode == 5 && i + 2 < num_params) {
            int idx = params[i + 2];
            if (idx >= 0) resolve_256(t->cur_bg, idx);
            i += 2;
          } else if (mode == 2 && i + 4 < num_params) {
            int r = params[i + 2];
            int g = params[i + 3];
            int b = params[i + 4];
            if (r >= 0) t->cur_bg[0] = (uint8_t)(r & 0xFF);
            if (g >= 0) t->cur_bg[1] = (uint8_t)(g & 0xFF);
            if (b >= 0) t->cur_bg[2] = (uint8_t)(b & 0xFF);
            i += 4;
          }
        }

      // ---- Extended underline colour: 58;5;N  or  58;2;R;G;B ----
      } else if (p == 58) {
        // Underline colour – same parsing as fg/bg extended
        if (i + 1 < num_params) {
          int mode = params[i + 1];
          if (mode == 5 && i + 2 < num_params) {
            i += 2;
          } else if (mode == 2 && i + 4 < num_params) {
            i += 4;
          }
        }
      }
    }
    break;
  }

  // ---- Scroll ----
  case 'S': { // SU – scroll up
    int n = PARAM(0); if (n < 0) n = 1;
    scroll_up(t, n);
    break;
  }
  case 'T': { // SD – scroll down
    int n = PARAM(0); if (n < 0) n = 1;
    scroll_down(t, n);
    break;
  }
  }

#undef PARAM
}

// ---------------------------------------------------------------------------
// ESC handler
// ---------------------------------------------------------------------------

/// @brief Handle a standalone ESC sequence (not CSI).
///
/// Implements DECSC/DECRC (save/restore cursor), IND (index), RI
/// (reverse index), NEL (next line), RIS (reset), and HTS (tab set).
///
/// @param intermediates      Intermediate bytes.
/// @param num_intermediates  Number of intermediates.
/// @param final              Final byte of the ESC sequence.
/// @param ctx                Pointer to terminal_t.
static void on_esc(char intermediates[2], int num_intermediates,
                   char final, void *ctx) {
  (void)intermediates; (void)num_intermediates;
  terminal_t *t = (terminal_t *)ctx;
  // An ESC sequence breaks any in-progress UTF-8 multi-byte sequence
  utf8_reset(t);

  switch (final) {
  default:
    break;
  case '7': // DECSC – save cursor
    t->saved_cx = t->cx; t->saved_cy = t->cy;
    break;
  case '8': // DECRC – restore cursor
    t->cx = t->saved_cx; t->cy = t->saved_cy;
    break;
  case 'D': // IND – index (scroll up if at bottom)
    t->cy++;
    if (t->cy >= t->screen.rows) {
      scroll_up(t, 1);
      t->cy = t->screen.rows - 1;
    }
    break;
  case 'M': // RI – reverse index (scroll down if at top)
    if (t->cy == 0) {
      scroll_down(t, 1);
    } else {
      t->cy--;
    }
    break;
  case 'E': // NEL – next line
    t->cx = 0; t->cy++;
    if (t->cy >= t->screen.rows) {
      scroll_up(t, 1);
      t->cy = t->screen.rows - 1;
    }
    break;
  case 'c': // RIS – reset to initial state
    clear_screen(t);
    reset_sgr(t);
    t->saved_cx = t->saved_cy = 0;
    break;
  case 'H': // HTS – set tab stop (ignored)
    break;
  }
}

/// @brief OSC handler (Operating System Command).
///
/// Handles:
///   - OSC 52 (clipboard control): ESC ] 52 ; <sel> ; <base64> [ST|BEL]
///
/// @param cmd  OSC command number.
/// @param str  OSC string payload.
/// @param ctx  Pointer to terminal_t.
static void on_osc(int cmd, const char *str, void *ctx) {
  terminal_t *t = (terminal_t *)ctx;
  utf8_reset(t);

  if (cmd == 52) {
    // OSC 52: ESC ] 52 ; <selection> ; <base64-data> [ST|BEL]
    // Parse: "c;BASE64" or "s;BASE64" or "p;BASE64"
    const char *p = str;
    if (*p && *(p + 1) == ';') {
      char sel = *p;
      p += 2; // skip "X;"

      if (*p) {
        // Decode base64 and copy to clipboard
        uint8_t dec[CLIPBOARD_MAX_SIZE];
        int len = base64_decode(p, dec, sizeof(dec) - 1);
        dec[len] = '\0';
        if (len > 0) {
          clipboard_copy((const char *)dec);
          // Also set PRIMARY if requested
          if (sel == 'p' || sel == 's')
            clipboard_copy_primary((const char *)dec);
        }
      }
    }
  }
}

/// @brief DCS handler (Device Control String) – currently a no-op.
/// @param cmd  First parameter.
/// @param str  DCS string payload.
/// @param ctx  Unused.
static void on_dcs(int cmd, const char *str, void *ctx) {
  (void)cmd; (void)str;
  terminal_t *t = (terminal_t *)ctx;
  utf8_reset(t);
}

// ---------------------------------------------------------------------------
// PTY helpers
// ---------------------------------------------------------------------------

/// @brief Fork a PTY and set initial window size.
///
/// The child executes the user's login shell (SHELL env var, or /bin/sh).
/// @param rows  Initial terminal rows (from config, updated after font load).
/// @param cols  Initial terminal columns (from config, updated after font load).
/// @return Master PTY file descriptor, or -1 on failure.
static int pty_fork(int rows, int cols) {
  int master_fd;
  struct winsize ws = {
    .ws_row = (unsigned short)rows,
    .ws_col = (unsigned short)cols,
    .ws_xpixel = 0,
    .ws_ypixel = 0,
  };
  pid_t pid = forkpty(&master_fd, NULL, NULL, &ws);
  if (pid == -1) { perror("forkpty"); return -1; }
  if (pid == 0) {
    const char *shell = getenv("SHELL");
    if (!shell) shell = "/bin/sh";
    execl(shell, shell, "-i", (char *)NULL);
    perror("execl");
    _exit(1);
  }
  return master_fd;
}

/// @brief Notify the child of a terminal window size change.
/// @param master_fd  Master PTY file descriptor.
/// @param cols       New number of columns.
/// @param rows       New number of rows.
static void pty_resize(int master_fd, int cols, int rows) {
  struct winsize ws = {
    .ws_row = (unsigned short)rows,
    .ws_col = (unsigned short)cols,
    .ws_xpixel = 0,
    .ws_ypixel = 0,
  };
  ioctl(master_fd, TIOCSWINSZ, &ws);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

/// @brief Render a single terminal cell with its SGR colours and styles.
///
/// Draws an optional background rectangle if the cell has a non-default
/// background colour, then renders the character glyph in the cell's
/// foreground colour.  If bold is set the glyph is drawn twice with a
/// 1 px horizontal offset (pseudo-bold).  If underline is set a
/// horizontal line is drawn below the glyph.
///
/// @param f     Font handle (must be initialised).
/// @param cp    Unicode codepoint to check.
/// @return      true if the font has a non-zero atlas rectangle for this glyph.
/// Check whether a font actually has renderable data for a given codepoint.
///
#if 0
/// GetGlyphIndex alone is NOT sufficient because LoadFontEx pre-allocates
/// glyph entries for every codepoint in the request list (setting .value),
/// but empty entries have zero atlas rectangles.
///
/// GetGlyphAtlasRec() is also NOT reliable because it has a built-in
/// fallback to '?' — if the codepoint is not found it silently returns
/// the rectangle for '?', making every check appear to succeed.
///
/// Instead we access f->recs[] directly (avoids the '?' fallback).
static bool font_has_glyph(const Font *f, int cp) {
  if (!f || f->texture.id == 0 || !f->glyphs || !f->recs) return false;
  int idx = GetGlyphIndex(*f, cp);
  if (idx < 0 || idx >= f->glyphCount) return false;
  // Direct recs array access — no '?' fallback.
  // Unfound glyphs have recs[idx] = {0,0,0,0} from GenTextureFontAtlas's
  // calloc; found glyphs have a non-zero rectangle.
  return f->recs[idx].width > 0 && f->recs[idx].height > 0;
}
#endif

#if 0
/// Pick the best font for rendering a codepoint.
/// Returns the font where the glyph is available, falling back
/// through: current (regular, e.g. Maple Mono) → nerd (NERD PUA icons)
/// → symbols (e.g. Noto Sans Symbols 2) → '?' fallback.
static __attribute__((unused)) Font* pick_glyph_font(font_handle_t *font, int *cp) {
  // Control chars → space
  if (*cp < 0x20 && *cp != '\t') *cp = ' ';

  // 1. Current (regular) font — best-looking letters for everyday text
  if (font_has_glyph(font->current, *cp))
    return font->current;

  // 2. Nerd Font — PUA icons (Powerline, Devicons, etc.) not in regular
  if (font_has_glyph(&font->nerd, *cp))
    return &font->nerd;

  // 3. Symbols fallback (Noto Sans Symbols 2 etc.)
  if (font_has_glyph(&font->symbols, *cp))
    return &font->symbols;

  // 4. '?' in current font (which always has ASCII)
  *cp = '?';
  return font->current;
}
#endif

static void render_cell(font_handle_t *font, const screen_cell_t *cell,
                        float x, float y, float char_w, float char_h) {
  (void)font; (void)cell; (void)x; (void)y; (void)char_w; (void)char_h;
  return;
#if 0
  if (!font || !cell) return;

  int cp = cell->ch;
  Font *render_font = pick_glyph_font(font, &cp);
  if (!render_font) return;

  bool has_bg = (cell->bg[0] != 0 || cell->bg[1] != 0 || cell->bg[2] != 0);

  // Draw background rectangle if non-default
  if (has_bg) {
    Color bg_col = { cell->bg[0], cell->bg[1], cell->bg[2], 255 };
    DrawRectangle((int)x, (int)y, (int)char_w, (int)char_h, bg_col);
  }

  // Draw foreground glyph
  Color fg_col = { cell->fg[0], cell->fg[1], cell->fg[2], 255 };

  // Bold effect: if bold, render twice with a 1px offset for a pseudo-bold look
  if (cell->bold) {
    DrawTextCodepoints(*render_font, &cp, 1,
                       (Vector2){ x + 1.0f, y },
                       font->font_size, font->spacing, fg_col);
  }

  DrawTextCodepoints(*render_font, &cp, 1,
                      (Vector2){ x, y },
                      font->font_size, font->spacing, fg_col);

  // Underline effect
  if (cell->underline) {
    float underline_y = y + font->font_size - 2.0f;
    DrawLine((int)x, (int)underline_y,
             (int)(x + char_w), (int)underline_y, fg_col);
  }
#endif
}

// ---------------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------------

/// @brief Check if a cell at (row, col) is within the selection rectangle.
static bool is_cell_selected(const terminal_t *t, int row, int col) {
    if (!t->selection.has_selection && !t->selection.active)
        return false;

    int r1 = t->selection.start_row;
    int c1 = t->selection.start_col;
    int r2 = t->selection.end_row;
    int c2 = t->selection.end_col;

    // Normalise (swap both row AND column when direction is reversed)
    if (r1 > r2) {
        int tmp = r1; r1 = r2; r2 = tmp;
        tmp = c1; c1 = c2; c2 = tmp;
    }
    if (r1 == r2 && c1 > c2) {
        int tmp = c1; c1 = c2; c2 = tmp;
    }

    if (row < r1 || row > r2) return false;
    if (row == r1 && col < c1) return false;
    if (row == r2 && col > c2) return false;
    return true;
}

/// @brief Extract selected text from the screen buffer into a buffer.
///        Returns the number of bytes written (excluding NUL).
static int extract_selected_text(terminal_t *t,
                                  char *buf, size_t buf_size) {
    if (!t->selection.has_selection || !buf || buf_size == 0)
        return 0;

    int r1 = t->selection.start_row;
    int c1 = t->selection.start_col;
    int r2 = t->selection.end_row;
    int c2 = t->selection.end_col;

    // Normalise
    if (r1 > r2) {
        int tmp = r1; r1 = r2; r2 = tmp;
        tmp = c1; c1 = c2; c2 = tmp;
    }
    if (r1 == r2 && c1 > c2) {
        int tmp = c1; c1 = c2; c2 = tmp;
    }

    int pos = 0;
    for (int r = r1; r <= r2 && pos < (int)buf_size - 1; r++) {
        int start_c = (r == r1) ? c1 : 0;
        int end_c   = (r == r2) ? c2 : t->screen.cols - 1;

        for (int c = start_c; c <= end_c && pos < (int)buf_size - 1; c++) {
            screen_cell_t *cell = screen_buf_cell(
                &t->screen, r, c);
            if (!cell) continue;
            int ch = cell->ch;
            if (ch < 0x20 && ch != '\t') continue; // skip C0 controls

            // Encode to UTF-8
            if (ch < 0x80) {
                buf[pos++] = (char)ch;
            } else if (ch < 0x800) {
                if (pos + 2 >= (int)buf_size) break;
                buf[pos++] = (char)(0xC0 | (ch >> 6));
                buf[pos++] = (char)(0x80 | (ch & 0x3F));
            } else if (ch < 0x10000) {
                if (pos + 3 >= (int)buf_size) break;
                buf[pos++] = (char)(0xE0 | (ch >> 12));
                buf[pos++] = (char)(0x80 | ((ch >> 6) & 0x3F));
                buf[pos++] = (char)(0x80 | (ch & 0x3F));
            } else if (ch < 0x110000) {
                if (pos + 4 >= (int)buf_size) break;
                buf[pos++] = (char)(0xF0 | (ch >> 18));
                buf[pos++] = (char)(0x80 | ((ch >> 12) & 0x3F));
                buf[pos++] = (char)(0x80 | ((ch >> 6) & 0x3F));
                buf[pos++] = (char)(0x80 | (ch & 0x3F));
            }
        }

        // Add newline between rows (but not after the last row)
        if (r < r2 && pos < (int)buf_size - 1) {
            buf[pos++] = '\n';
        }
    }

    buf[pos] = '\0';
    return pos;
}

/// @brief Copy selected text to the system clipboard.
static void copy_selection(terminal_t *t) {
    char sel_buf[CLIPBOARD_MAX_SIZE];
    int len = extract_selected_text(t, sel_buf, sizeof(sel_buf));
    if (len > 0) {
        clipboard_copy(sel_buf);
    }
}

// ---------------------------------------------------------------------------
// Clipboard shortcut handling
// ---------------------------------------------------------------------------

/// @brief Write all bytes to a fd, retrying on partial writes (forward decl).
static int write_all(int fd, const void *buf, size_t len);

/** @brief Result of clipboard shortcut processing. */
typedef enum {
    CLIP_ACTION_NONE,         /**< No clipboard action. */
    CLIP_ACTION_COPY,         /**< Selection was copied. */
    CLIP_ACTION_PASTE,        /**< Clipboard was pasted to PTY. */
    CLIP_ACTION_PASTE_PRIMARY,/**< PRIMARY selection was pasted to PTY. */
} clip_action_t;

/**
 * @brief Check for clipboard-related keyboard shortcuts.
 *
 * Intercepts Ctrl+Shift+C (copy), Ctrl+Shift+V (paste), and
 * Shift+Insert (paste primary) before they reach the PTY.
 *
 * @param t         Terminal state.
 * @param master_fd PTY master file descriptor for paste writes.
 * @return          The action that was performed (or CLIP_ACTION_NONE).
 */
static clip_action_t handle_clipboard_shortcuts(terminal_t *t, int master_fd) {
    bool ctrl  = input_key_down(KEY_LEFT_CONTROL) || input_key_down(KEY_RIGHT_CONTROL);
    bool shift = input_key_down(KEY_LEFT_SHIFT)   || input_key_down(KEY_RIGHT_SHIFT);
    bool alt   = input_key_down(KEY_LEFT_ALT)     || input_key_down(KEY_RIGHT_ALT);

    // Ctrl+Shift+C — copy selection to clipboard
    if (ctrl && shift && !alt && input_key_pressed(KEY_C)) {
        input_consume_key(KEY_C);
        copy_selection(t);
        return CLIP_ACTION_COPY;
    }

    // Ctrl+Shift+V — paste from clipboard
    if (ctrl && shift && !alt && input_key_pressed(KEY_V)) {
        input_consume_key(KEY_V);
        const char *text = clipboard_paste();
        if (text && text[0]) {
            if (t->bracketed_paste) {
                write_all(master_fd, "\x1B[200~", 6);
                write_all(master_fd, text, strlen(text));
                write_all(master_fd, "\x1B[201~", 6);
            } else {
                write_all(master_fd, text, strlen(text));
            }
        }
        return CLIP_ACTION_PASTE;
    }

    // Shift+Insert — paste from PRIMARY selection
    if (shift && !ctrl && !alt && input_key_pressed(KEY_INSERT)) {
        input_consume_key(KEY_INSERT);
        const char *text = clipboard_paste_primary();
        if (!text || !text[0])
            text = clipboard_paste(); // fallback to CLIPBOARD
        if (text && text[0]) {
            if (t->bracketed_paste) {
                write_all(master_fd, "\x1B[200~", 6);
                write_all(master_fd, text, strlen(text));
                write_all(master_fd, "\x1B[201~", 6);
            } else {
                write_all(master_fd, text, strlen(text));
            }
        }
        return CLIP_ACTION_PASTE_PRIMARY;
    }

    return CLIP_ACTION_NONE;
}

/**
 * @brief Write all bytes to a file descriptor, retrying on partial writes.
 * @return Number of bytes written, or -1 on error.
 */
static int write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t r = write(fd, p + total, len - total);
        if (r > 0) {
            total += (size_t)r;
        } else if (r == 0) {
            return -1;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000); // yield to avoid busy-wait
                continue;
            }
            return -1;
        }
    }
    return (int)total;
}

/// @brief Handle middle-click paste from PRIMARY selection.
static void handle_middle_click_paste(terminal_t *t, int master_fd) {
    const char *text = clipboard_paste_primary();
    if (!text || !text[0]) return;
    if (t->bracketed_paste) {
        write_all(master_fd, "\x1B[200~", 6);
        write_all(master_fd, text, strlen(text));
        write_all(master_fd, "\x1B[201~", 6);
    } else {
        write_all(master_fd, text, strlen(text));
    }
}

/// @brief Handle mouse-based text selection (when mouse tracking is OFF).
static void handle_mouse_selection(terminal_t *t, GLFWwindow *window,
                                    int master_fd,
                                    float char_w, float char_h) {
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);

    // Convert to cell coordinates
    int col = (int)((mx - (double)WIN_PADDING) / (double)char_w);
    int row = (int)((my - (double)WIN_PADDING) / (double)char_h);
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (col >= t->screen.cols) col = t->screen.cols - 1;
    if (row >= t->screen.rows) row = t->screen.rows - 1;

    int left_btn   = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
    int middle_btn = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE);
    static int prev_left   = GLFW_RELEASE;
    static int prev_middle = GLFW_RELEASE;

    // Middle-click paste
    if (prev_middle == GLFW_PRESS && middle_btn == GLFW_RELEASE) {
        handle_middle_click_paste(t, master_fd);
        t->selection.has_selection = false;
        t->selection.active = false;
    }

    // Left button state change
    if (prev_left == GLFW_PRESS && left_btn == GLFW_RELEASE) {
        // Released — finalize selection
        if (t->selection.active) {
            t->selection.end_row = row;
            t->selection.end_col = col;
            t->selection.active = false;
            t->selection.has_selection =
                (t->selection.start_row != t->selection.end_row ||
                 t->selection.start_col != t->selection.end_col);
        }
    } else if (prev_left == GLFW_RELEASE && left_btn == GLFW_PRESS) {
        // Pressed — start new selection
        t->selection.start_row = row;
        t->selection.start_col = col;
        t->selection.end_row = row;
        t->selection.end_col = col;
        t->selection.active = true;
        t->selection.has_selection = false;
    } else if (left_btn == GLFW_PRESS && t->selection.active) {
        // Dragging — extend selection
        t->selection.end_row = row;
        t->selection.end_col = col;
    }

    prev_left   = left_btn;
    prev_middle = middle_btn;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

/**
 * @brief Terminal emulator entry point.
 *
 * Initialises (in order):
 *   1. PTY master/slave via forkpty() (must happen before raylib init)
 *   2. Raylib window (resizable)
 *   3. Font subsystem (auto-discovers regular, Nerd, and symbol fonts)
 *   4. Screen buffer, scrollback, terminal state
 *   5. Parser callbacks wired to terminal_t methods
 *
 * Main loop:
 *   - Drains PTY output and feeds it to the parser
 *   - Adjusts scrollback viewport when new output arrives while browsing history
 *   - Handles scrollback navigation keys
 *   - Forwards keyboard input to the PTY
 *   - Resizes PTY on window resize
 *   - Detects and recovers from stuck parser states
 *   - Renders the screen (normal or scrollback viewport mode)
 *
 * @return 0 on success, 1 on initialisation failure.
 */
int main(void) {
  // ── Load configuration ──────────────────────────────────────────────
  const dicterm_config_t cfg = config_load();
  WIN_WIDTH = cfg.win_width;
  WIN_HEIGHT = cfg.win_height;
  WIN_PADDING = cfg.win_padding;
  memcpy(default_fg, cfg.default_fg, sizeof(default_fg));
  memcpy(default_bg, cfg.default_bg, sizeof(default_bg));

  // PTY must be forked BEFORE raylib/GLFW initialisation, because
  // raylib installs signal handlers and may spawn helper threads
  // that would interfere with the child process after forkpty().
  int master_fd = pty_fork(cfg.rows, cfg.cols);
  if (master_fd < 0) {
    fprintf(stderr, "dicTerm: pty_fork failed.\n");
    return 1;
  }

  int fl = fcntl(master_fd, F_GETFL, 0);
  if (fl == -1) { perror("fcntl F_GETFL"); fl = 0; }
  if (fcntl(master_fd, F_SETFL, fl | O_NONBLOCK) == -1)
    perror("fcntl F_SETFL");

#if defined(GLFW_PLATFORM)
  {
    const char *dpy = getenv("DISPLAY");
    if (dpy && dpy[0])
      glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  }
#endif
  if (!glfwInit()) {
    fprintf(stderr, "dicTerm: glfwInit failed.\n");
    close(master_fd);
    return 1;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  if (cfg.clear_bg)
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
  GLFWwindow *window = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, "dicTerm", NULL, NULL);
  if (!window) {
    const char *desc = NULL;
    glfwGetError(&desc);
    fprintf(stderr, "dicTerm: glfwCreateWindow failed.\n");
    if (desc) fprintf(stderr, "  GLFW error: %s\n", desc);
    const char *wl = getenv("WAYLAND_DISPLAY");
    const char *x11 = getenv("DISPLAY");
    fprintf(stderr, "  Platform: WAYLAND_DISPLAY=%s  DISPLAY=%s\n",
            wl ? wl : "(unset)", x11 ? x11 : "(unset)");
    if (wl && wl[0] && (!x11 || !x11[0]))
      fprintf(stderr, "  Tip: restart waypipe with --xwls, then set DISPLAY=:0\n");
    glfwTerminate(); close(master_fd); return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  // ── Background blur ──────────────────────────────────────────────────
  // Requests the compositor to blur the desktop behind the terminal.
  // On X11 this uses the _KDE_NET_WM_BLUR_BEHIND_REGION atom (supported
  // by KWin and other compositors that implement the KDE blur protocol).
  // The region is re-applied on resize so it always covers the window.
#if defined(HAS_X11)
  static unsigned long blur_rect[4] = {0, 0, 0, 0};
  static Atom blur_atom = None;
  static Display *x11_dpy = NULL;
  static Window x11_win = 0;
  if (cfg.bg_blur) {
    x11_dpy = glfwGetX11Display();
    x11_win = glfwGetX11Window(window);
    if (x11_dpy && x11_win) {
      blur_atom = XInternAtom(x11_dpy, "_KDE_NET_WM_BLUR_BEHIND_REGION", False);
      blur_rect[2] = (unsigned long)WIN_WIDTH;
      blur_rect[3] = (unsigned long)WIN_HEIGHT;
      if (blur_atom != None)
        XChangeProperty(x11_dpy, x11_win, blur_atom, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char*)blur_rect, 4);
    }
  }
#else
  (void)cfg;
#endif

  input_init(window);
  clipboard_init(window);

  // Font subsystem
  font_handle_t *font = font_init(
      cfg.font_regular[0] ? cfg.font_regular : NULL,
      cfg.font_nerd[0]    ? cfg.font_nerd    : NULL,
      cfg.font_size);
  if (!font) {
    fprintf(stderr, "dicTerm: Failed to initialise fonts.\n");
    close(master_fd);
    glfwDestroyWindow(window); glfwTerminate();
    return 1;
  }
  gl_renderer_t *gl_renderer = gl_renderer_create(font);
  if (gl_renderer) {
    gl_renderer_set_clear_bg(gl_renderer, cfg.clear_bg);
    gl_renderer_set_bg_opacity(gl_renderer, cfg.bg_opacity);
  }
  if (!gl_renderer) {
    fprintf(stderr, "dicTerm: Failed to initialise the OpenGL renderer.\n");
    font_uninit(font);
    close(master_fd);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  // Show which fonts are loaded in the window title
  {
    char title[256];
    const char *reg = font->regular_path[0] ? font->regular_path : "default";
    const char *nerd_name = font->nerd_path[0] ? font->nerd_path : "none";
    const char *sym = font_find_symbols_path();
    if (!sym) sym = "none";
    // Extract just the filename for readability
    const char *reg_file = strrchr(reg, '/');
    reg_file = reg_file ? reg_file + 1 : reg;
    const char *nerd_file = strrchr(nerd_name, '/');
    nerd_file = nerd_file ? nerd_file + 1 : nerd_name;
    const char *sym_file = strrchr(sym, '/');
    sym_file = sym_file ? sym_file + 1 : sym;
    snprintf(title, sizeof(title), "dicTerm [%s] nerd=%s sym=%s",
             reg_file, nerd_file, sym_file);
    glfwSetWindowTitle(window, title);
  }

  float char_w = font_char_width(font);
  float char_h = font_char_height(font);

  // Compute terminal grid dimensions from window size and font metrics
  int term_rows = (WIN_HEIGHT - WIN_PADDING * 2) / (int)char_h;
  int term_cols = (WIN_WIDTH - WIN_PADDING * 2) / (int)char_w;
  if (term_rows < 1) term_rows = 1;
  if (term_cols < 1) term_cols = 1;

  // Terminal state
  terminal_t term = {0};
  {
    screen_buf_t *sb = screen_buf_new(term_rows, term_cols);
    if (!sb) {
      close(master_fd);
      gl_renderer_destroy(gl_renderer);
      font_uninit(font);
      glfwDestroyWindow(window); glfwTerminate();
      return 1;
    }
    term.screen = *sb;  // copy the struct (includes cells pointer)
    free(sb);           // free the wrapper only; cells survive via term.screen.cells
  }

  term.scrollback = scrollback_create(cfg.scrollback_capacity, term_cols);
  if (!term.scrollback) {
    free(term.screen.cells);
    close(master_fd);
    gl_renderer_destroy(gl_renderer);
    font_uninit(font);
    glfwDestroyWindow(window); glfwTerminate();
    return 1;
  }

  term.font = font;
  term.win_width  = WIN_WIDTH;
  term.win_height = WIN_HEIGHT;
  reset_sgr(&term);

  mouse_init(window, term_cols, term_rows, WIN_PADDING, char_w, char_h);

  parser_callbacks_t cbs = {
    .on_print   = on_print,
    .on_execute = on_execute,
    .on_csi     = on_csi,
    .on_esc     = on_esc,
    .on_osc     = on_osc,
    .on_dcs     = on_dcs,
  };
  parser_init(&term.parser, &cbs, &term);

  // Parser stuck detection
  enum { STUCK_FRAME_LIMIT = 60 };
  int parser_stuck_frames = 0;
  parser_state_t prev_parser_state = PARSER_GROUND;

  // Reusable codepoint scratch buffer for scrollback viewport rendering
  // (allocated once; size tracks the current screen width in columns).
  int *line_buf = malloc((size_t)term_cols * sizeof(int));
  if (!line_buf) {
    fprintf(stderr, "dicTerm: Out of memory.\n");
    close(master_fd);
    scrollback_destroy(term.scrollback);
    free(term.screen.cells);
    gl_renderer_destroy(gl_renderer);
    font_uninit(font);
    glfwDestroyWindow(window); glfwTerminate();
    return 1;
  }

  // ---- Main loop ----
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    // Capture scrollback total before draining PTY, so we can detect
    // how many new lines were pushed and adjust the viewport offset
    // to keep the user's scroll position stable.
    int sb_total_before = scrollback_total_pushed(term.scrollback);

    // Drain PTY output
    bool in_error_state = false;
    for (;;) {
      char buf[65536];
      ssize_t n = read(master_fd, buf, sizeof(buf));
      if (n > 0) {
        parser_feed(&term.parser, (const uint8_t *)buf, (size_t)n);
      } else if (n == 0) {
        in_error_state = true;
        break;
      } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EIO) { in_error_state = true; break; }
        perror("read");
        in_error_state = true;
        break;
      }
    }
    if (in_error_state) break;

    // If new lines were pushed to the scrollback while we were in
    // scrollback mode, adjust the offset so the viewport stays
    // pointing at the same logical content.
    if (term.scroll_offset > 0) {
      int sb_total_after = scrollback_total_pushed(term.scrollback);
      int new_pushes = sb_total_after - sb_total_before;
      if (new_pushes > 0) {
        term.scroll_offset += new_pushes;
        int max_offset = scrollback_count(term.scrollback);
        if (term.scroll_offset > max_offset)
          term.scroll_offset = max_offset;
      }
    }

    // Handle scrollback navigation keys (PageUp/Down, Shift+Up/Down)
    // This must run BEFORE process_keyboard_input because it uses IsKeyPressed
    // which does not consume events from GetKeyPressed().
    handle_scrollback_keys(&term);

    // Handle clipboard shortcuts (Ctrl+Shift+C, Ctrl+Shift+V, Shift+Insert).
    // Must run BEFORE process_keyboard_input so we can consume these keys.
    clip_action_t clip_action = handle_clipboard_shortcuts(&term, master_fd);

    // Keyboard input → PTY (clipboard keys are consumed and won't be forwarded)
    int written = process_keyboard_input(master_fd);

    // Clear selection on paste (content was written to PTY)
    if (clip_action == CLIP_ACTION_PASTE ||
        clip_action == CLIP_ACTION_PASTE_PRIMARY) {
      term.selection.has_selection = false;
    }

    // Clear selection on any regular (non-clipboard) keyboard input
    if (written > 0 && clip_action == CLIP_ACTION_NONE) {
      term.selection.has_selection = false;
      term.selection.active = false;
    }

    // If any regular key was sent to the PTY while in scrollback mode,
    // exit scrollback mode (return to normal terminal view).
    if (written > 0 && term.scroll_offset > 0)
      term.scroll_offset = 0;

    // Window resize → PTY
    int new_w = 0;
    int new_h = 0;
    glfwGetFramebufferSize(window, &new_w, &new_h);
    if (new_w != term.win_width || new_h != term.win_height) {
      term.win_width  = new_w;
      term.win_height = new_h;

#if defined(HAS_X11)
      if (blur_atom != None) {
        blur_rect[2] = (unsigned long)new_w;
        blur_rect[3] = (unsigned long)new_h;
        XChangeProperty(x11_dpy, x11_win, blur_atom, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char*)blur_rect, 4);
      }
#endif

      int new_cols = (new_w - WIN_PADDING * 2) / (int)char_w;
      int new_rows = (new_h - WIN_PADDING * 2) / (int)char_h;
      if (new_cols < 1) new_cols = 1;
      if (new_rows < 1) new_rows = 1;
      pty_resize(master_fd, new_cols, new_rows);
      screen_buf_resize(&term.screen, new_rows, new_cols);
      if (term.alt_screen.cells)
        screen_buf_resize(&term.alt_screen, new_rows, new_cols);
      mouse_update_geometry(new_cols, new_rows, WIN_PADDING, char_w, char_h);
      // Clamp cursor position after resize to prevent OOB access in render
      if (term.cx >= new_cols) term.cx = new_cols - 1;
      if (term.cy >= new_rows) term.cy = new_rows - 1;
    }

    // Forward mouse events to PTY when mouse tracking is enabled
    if (mouse_is_enabled()) {
      if (process_mouse_input(master_fd) < 0) break;
    } else {
      handle_mouse_selection(&term, window, master_fd, char_w, char_h);
    }

    // Parser stuck detection
    {
      parser_state_t cur = term.parser.state;
      if (cur != PARSER_GROUND) {
        if (cur == prev_parser_state) {
          parser_stuck_frames++;
          if (parser_stuck_frames >= STUCK_FRAME_LIMIT) {
            utf8_reset(&term);
            parser_reset(&term.parser);
            parser_stuck_frames = 0;
          }
        } else {
          parser_stuck_frames = 0;
        }
      } else {
        parser_stuck_frames = 0;
      }
      prev_parser_state = cur;
    }

    // ---- Render ----
    glViewport(0, 0, term.win_width, term.win_height);
    glClearColor((float)default_bg[0] / 255.0f,
                 (float)default_bg[1] / 255.0f,
                 (float)default_bg[2] / 255.0f,
                 cfg.clear_bg ? cfg.bg_opacity : 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (gl_renderer) gl_renderer_begin(gl_renderer, term.win_width, term.win_height);

    int screen_rows = term.screen.rows;
    int sb_count = scrollback_count(term.scrollback);

    if (term.scroll_offset > 0) {
      // ── Scrollback (viewport) mode ──────────────────────────────
      // The viewport is shifted up by scroll_offset lines.
      // Top portion: scrollback lines (old history).
      // Bottom portion: the current visible screen (shifted up).
      int *line = line_buf;
      for (int r = 0; r < screen_rows; r++) {
        float y = (float)WIN_PADDING + (float)r * char_h;

        // Index into the scrollback buffer for this row.
        // When r=0 we show the oldest scrollback line of the viewport.
        // When r=scroll_offset-1 we show the newest scrollback line.
        int sb_idx = term.scroll_offset - 1 - r;

        if (sb_idx >= 0 && sb_idx < sb_count) {
          // Render from scrollback buffer (codepoints, default colours)
          if (scrollback_get(term.scrollback, sb_idx, line, term_cols)) {
            screen_cell_t *scroll_cells = calloc((size_t)term.screen.cols,
                                                  sizeof(*scroll_cells));
            int sb_render_cols = term_cols < term.screen.cols ? term_cols : term.screen.cols;
            for (int c = 0; c < sb_render_cols; c++) {
              if (scroll_cells) {
                scroll_cells[c].ch = line[c];
                scroll_cells[c].fg[0] = default_fg[0];
                scroll_cells[c].fg[1] = default_fg[1];
                scroll_cells[c].fg[2] = default_fg[2];
              }
            }
            if (scroll_cells) {
              if (gl_renderer)
                gl_renderer_draw_cells(gl_renderer, scroll_cells,
                                       term.screen.cols,
                                       (float)WIN_PADDING, y,
                                       char_w, char_h);
              free(scroll_cells);
            }
          }
        } else {
          // Scrollback lines exhausted – show the corresponding screen line
          int screen_row = r - term.scroll_offset;
          if (screen_row >= 0 && screen_row < term.screen.rows) {
            if (gl_renderer)
              gl_renderer_draw_cells(gl_renderer,
                                     &term.screen.cells[screen_row * term.screen.cols],
                                     term.screen.cols, (float)WIN_PADDING, y,
                                     char_w, char_h);
            for (int c = 0; c < term.screen.cols; c++) {
              float x = (float)WIN_PADDING + (float)c * char_w;
              const screen_cell_t *cell =
                &term.screen.cells[screen_row * term.screen.cols + c];
              render_cell(term.font, cell, x, y, char_w, char_h);
            }
          }
          // If screen_row is also out of range, the row is left blank.
        }
      }

    } else {
      // ── Normal terminal mode ────────────────────────────────────
      bool has_sel = term.selection.has_selection || term.selection.active;

      for (int r = 0; r < term.screen.rows; r++) {
        float y = (float)WIN_PADDING + (float)r * char_h;

        if (has_sel) {
          // Copy the row and swap fg/bg for selected cells
          screen_cell_t *row_cells = NULL;
          // Stack-allocate a small buffer if cols is small, else malloc
          screen_cell_t stack_buf[256];
          bool use_stack = term.screen.cols <= 256;
          if (use_stack) {
            row_cells = stack_buf;
          } else {
            row_cells = malloc((size_t)term.screen.cols * sizeof(screen_cell_t));
            if (!row_cells) continue;
          }

          memcpy(row_cells,
                 &term.screen.cells[(size_t)r * (size_t)term.screen.cols],
                 (size_t)term.screen.cols * sizeof(screen_cell_t));

          for (int c = 0; c < term.screen.cols; c++) {
            if (is_cell_selected(&term, r, c)) {
              // Swap fg and bg for selection highlight
              uint8_t tmp_fg[3] = { row_cells[c].fg[0],
                                    row_cells[c].fg[1],
                                    row_cells[c].fg[2] };
              row_cells[c].fg[0] = row_cells[c].bg[0];
              row_cells[c].fg[1] = row_cells[c].bg[1];
              row_cells[c].fg[2] = row_cells[c].bg[2];
              row_cells[c].bg[0] = tmp_fg[0];
              row_cells[c].bg[1] = tmp_fg[1];
              row_cells[c].bg[2] = tmp_fg[2];
            }
          }

          if (gl_renderer)
            gl_renderer_draw_cells(gl_renderer, row_cells,
                                   term.screen.cols,
                                   (float)WIN_PADDING, y, char_w, char_h);

          if (!use_stack) free(row_cells);
        } else {
          if (gl_renderer)
            gl_renderer_draw_cells(gl_renderer,
                                   &term.screen.cells[r * term.screen.cols],
                                   term.screen.cols,
                                   (float)WIN_PADDING, y, char_w, char_h);
        }
      }

      // ── Cursor rendering (active) ─────────────────────────────────
      {
        screen_cell_t cursor_cell;
        int cp = term.screen.cells[term.cy * term.screen.cols + term.cx].ch;
        if (cp < 0x20) cp = ' ';
        cursor_cell.ch = cp;
        cursor_cell.fg[0] = 0;   cursor_cell.fg[1] = 0;   cursor_cell.fg[2] = 0;
        cursor_cell.bg[0] = 255; cursor_cell.bg[1] = 255; cursor_cell.bg[2] = 100;
        cursor_cell.bold = 0;
        cursor_cell.italic = 0;
        cursor_cell.underline = 0;
        cursor_cell.blink = 0;

        float cx = (float)WIN_PADDING + (float)term.cx * char_w;
        float cy = (float)WIN_PADDING + (float)term.cy * char_h;

        if (gl_renderer)
          gl_renderer_draw_cells(gl_renderer, &cursor_cell, 1,
                                 cx, cy, char_w, char_h);
      }
    }

    if (gl_renderer) gl_renderer_end(gl_renderer);
    glfwSwapBuffers(window);
  }

  close(master_fd);
  scrollback_destroy(term.scrollback);
  free(term.screen.cells);
  if (term.alt_screen.cells) free(term.alt_screen.cells);
  free(line_buf);
  gl_renderer_destroy(gl_renderer);
  font_uninit(font);
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
