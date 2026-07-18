#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <raylib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "parser.h"
#include "font.h"
#include "input.h"
#include "scrollback.h"
#include "screen.h"

#define ROWS 36
#define COLS 100
#define WIN_PADDING 10
#define WIN_WIDTH  1280
#define WIN_HEIGHT 800

/**
 * @file main.c
 * @brief Terminal emulator entry point and main loop.
 *
 * Spawns a child shell via forkpty, parses its output with the ANSI
 * escape sequence state machine (parser.c), stores characters together
 * with SGR colour attributes in a screen_buf_t grid, and renders each
 * cell with its individual colours using raylib's font subsystem.
 */

typedef struct {
  screen_buf_t  screen;         // visible grid with SGR attributes
  int           cx, cy;         // cursor position (0-based)
  int           saved_cx, saved_cy;  // DECSC/DECRC saved position

  // Current SGR state (applied to subsequent cells)
  uint8_t       cur_fg[3];      // current foreground colour
  uint8_t       cur_bg[3];      // current background colour
  bool          cur_bold;
  bool          cur_underline;

  parser_t      parser;
  scrollback_t *scrollback;
  int           scroll_offset;   // 0 = normal view, > 0 = scrolled back into history
  font_handle_t *font;
  int           win_width;
  int           win_height;
} terminal_t;

// ---------------------------------------------------------------------------
// Colour tables for ANSI SGR codes
// ---------------------------------------------------------------------------

// Standard ANSI colours (codes 30-37 and 40-47)
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

// Bright ANSI colours (codes 90-97 and 100-107)
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

// Default colours (used when SGR 0 is sent)
static const uint8_t default_fg[3] = {220, 220, 220};
static const uint8_t default_bg[3] = {  0,   0,   0};

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
  int cols = t->screen.cols;
  char line_buf[COLS] = {0};
  for (int n = 0; n < count; n++) {
    // Copy the top row's characters into a plain buffer for scrollback
    for (int c = 0; c < COLS && c < cols; c++)
      line_buf[c] = t->screen.cells[c].ch;
    scrollback_push(t->scrollback, line_buf);

    // Shift all rows up by one
    memmove(t->screen.cells,
            t->screen.cells + cols,
            (size_t)(t->screen.rows - 1) * cols * sizeof(screen_cell_t));
    // Clear the new bottom row
    for (int c = 0; c < cols; c++) {
      screen_cell_t *cell = &t->screen.cells[(t->screen.rows - 1) * cols + c];
      cell->ch = ' ';
      cell->fg[0] = default_fg[0]; cell->fg[1] = default_fg[1]; cell->fg[2] = default_fg[2];
      cell->bg[0] = default_bg[0]; cell->bg[1] = default_bg[1]; cell->bg[2] = default_bg[2];
      cell->bold = cell->italic = cell->underline = cell->blink = 0;
    }
  }
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
            (size_t)(t->screen.rows - 1) * cols * sizeof(screen_cell_t));
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

  if (IsKeyPressed(KEY_PAGE_UP)) {
    t->scroll_offset += t->screen.rows;
    if (t->scroll_offset > max_offset)
      t->scroll_offset = max_offset;
  }
  if (IsKeyPressed(KEY_PAGE_DOWN)) {
    t->scroll_offset -= t->screen.rows;
    if (t->scroll_offset < 0)
      t->scroll_offset = 0;
  }

  bool shift_held = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
  if (shift_held) {
    if (IsKeyPressed(KEY_UP)) {
      t->scroll_offset++;
      if (t->scroll_offset > max_offset)
        t->scroll_offset = max_offset;
    }
    if (IsKeyPressed(KEY_DOWN)) {
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

/// @brief Handle a printable character from the child shell.
///
/// Writes the character into the current cursor cell, inheriting the
/// active SGR colour attributes (cur_fg, cur_bg, bold, underline).
/// Advances the cursor; scrolls up if the cursor moves past the last row.
///
/// @param ch   Character byte.
/// @param ctx  Pointer to terminal_t (opaque context).
static void on_print(uint8_t ch, void *ctx) {
  terminal_t *t = (terminal_t *)ctx;
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
    // Inherit current SGR state
    cell->fg[0] = t->cur_fg[0]; cell->fg[1] = t->cur_fg[1]; cell->fg[2] = t->cur_fg[2];
    cell->bg[0] = t->cur_bg[0]; cell->bg[1] = t->cur_bg[1]; cell->bg[2] = t->cur_bg[2];
    cell->bold = t->cur_bold;
    cell->underline = t->cur_underline;
  }
  t->cx++;
}

/// @brief Handle a C0 control character.
///
/// Processes LF, CR, BS, HT, VT, FF by moving the cursor or scrolling.
///
/// @param c0   Control character byte (0x00–0x1F, excluding ESC).
/// @param ctx  Pointer to terminal_t.
static void on_execute(char c0, void *ctx) {
  terminal_t *t = (terminal_t *)ctx;
  switch (c0) {
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
  (void)intermediates; (void)num_intermediates;
  terminal_t *t = (terminal_t *)ctx;

#define PARAM(n) ((n) < num_params && params[(n)] >= 0 ? params[(n)] : -1)

  switch (final) {
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

  switch (final) {
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

/// @brief OSC handler (Operating System Command) – currently a no-op.
/// @param cmd  OSC command number.
/// @param str  OSC string payload.
/// @param ctx  Unused.
static void on_osc(int cmd, const char *str, void *ctx) {
  (void)cmd; (void)str; (void)ctx;
}

/// @brief DCS handler (Device Control String) – currently a no-op.
/// @param cmd  First parameter.
/// @param str  DCS string payload.
/// @param ctx  Unused.
static void on_dcs(int cmd, const char *str, void *ctx) {
  (void)cmd; (void)str; (void)ctx;
}

// ---------------------------------------------------------------------------
// PTY helpers
// ---------------------------------------------------------------------------

/// @brief Fork a child process attached to a pseudo-terminal.
///
/// The child executes the user's login shell (SHELL env var, or /bin/sh).
/// Returns the master file descriptor for communication.
///
/// @return Master PTY file descriptor, or -1 on failure.
static int pty_fork(void) {
  int master_fd;
  struct winsize ws = {
    .ws_row = ROWS,
    .ws_col = COLS,
    .ws_xpixel = 0,
    .ws_ypixel = 0,
  };
  pid_t pid = forkpty(&master_fd, NULL, NULL, &ws);
  if (pid == -1) { perror("forkpty"); return -1; }
  if (pid == 0) {
    const char *shell = getenv("SHELL");
    if (!shell) shell = "/bin/sh";
    execl(shell, shell, NULL);
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
/// @param font    Font handle (must be initialised).
/// @param cell    Screen cell with character and colour attributes.
/// @param x       Pixel X position of the cell.
/// @param y       Pixel Y position of the cell.
/// @param char_w  Character width in pixels.
/// @param char_h  Character height in pixels.
static void render_cell(font_handle_t *font, const screen_cell_t *cell,
                        float x, float y, float char_w, float char_h) {
  if (!font || !cell) return;

  // Determine the glyph to draw
  int cp = cell->ch;
  if (cp < 0x20 && cp != '\t')
    cp = ' ';
  if (cp >= 0x80)
    cp = cell->ch; // pass through UTF-8 leader bytes

  bool has_bg = (cell->bg[0] != 0 || cell->bg[1] != 0 || cell->bg[2] != 0);

  // Draw background rectangle if non-default
  if (has_bg) {
    Color bg_col = { cell->bg[0], cell->bg[1], cell->bg[2], 255 };
    DrawRectangle((int)x, (int)y, (int)char_w, (int)char_h, bg_col);
  }

  // Draw foreground glyph
  Color fg_col = { cell->fg[0], cell->fg[1], cell->fg[2], 255 };

  // Bold effect: if bold, render twice with a 1px offset for a pseudo-bold look
  if (cell->bold && font->current) {
    DrawTextCodepoints(*font->current, &cp, 1,
                       (Vector2){ x + 1.0f, y + font->ascent },
                       font->font_size, font->spacing, fg_col);
  }

  if (font->current) {
    DrawTextCodepoints(*font->current, &cp, 1,
                       (Vector2){ x, y + font->ascent },
                       font->font_size, font->spacing, fg_col);
  }

  // Underline effect
  if (cell->underline) {
    float underline_y = y + font->font_size - 2.0f;
    DrawLine((int)x, (int)underline_y,
             (int)(x + char_w), (int)underline_y, fg_col);
  }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(WIN_WIDTH, WIN_HEIGHT, "dicTerm");

  // Font subsystem
  font_handle_t *font = font_init(NULL, NULL, 20.0f);
  if (!font) {
    fprintf(stderr, "dicTerm: Failed to initialise fonts.\n");
    CloseWindow();
    return 1;
  }
  SetTargetFPS(60);

  float char_w = font_char_width(font);
  float char_h = font_char_height(font);

  // PTY
  int master_fd = pty_fork();
  if (master_fd < 0) {
    font_uninit(font);
    CloseWindow();
    return 1;
  }

  int fl = fcntl(master_fd, F_GETFL, 0);
  if (fl == -1) { perror("fcntl F_GETFL"); fl = 0; }
  if (fcntl(master_fd, F_SETFL, fl | O_NONBLOCK) == -1)
    perror("fcntl F_SETFL");

  // Terminal state
  terminal_t term = {0};
  term.screen = *screen_buf_new(ROWS, COLS);
  if (!term.screen.cells) {
    close(master_fd);
    font_uninit(font);
    CloseWindow();
    return 1;
  }

  term.scrollback = scrollback_create(SCROLLBACK_CAPACITY, COLS);
  if (!term.scrollback) {
    screen_buf_free(&term.screen);
    close(master_fd);
    font_uninit(font);
    CloseWindow();
    return 1;
  }

  term.font = font;
  term.win_width  = WIN_WIDTH;
  term.win_height = WIN_HEIGHT;
  reset_sgr(&term);

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

  // ---- Main loop ----
  while (!WindowShouldClose()) {
    // Capture scrollback total before draining PTY, so we can detect
    // how many new lines were pushed and adjust the viewport offset
    // to keep the user's scroll position stable.
    int sb_total_before = scrollback_total_pushed(term.scrollback);

    // Drain PTY output
    for (;;) {
      char buf[65536];
      ssize_t n = read(master_fd, buf, sizeof(buf));
      if (n > 0) {
        parser_feed(&term.parser, (const uint8_t *)buf, (size_t)n);
      } else if (n == 0) {
        goto done;
      } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        perror("read");
        goto done;
      }
    }

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

    // Keyboard input → PTY
    int written = process_keyboard_input(master_fd);

    // If any regular key was sent to the PTY while in scrollback mode,
    // exit scrollback mode (return to normal terminal view).
    if (written > 0 && term.scroll_offset > 0)
      term.scroll_offset = 0;

    // Window resize → PTY
    int new_w = GetScreenWidth();
    int new_h = GetScreenHeight();
    if (new_w != term.win_width || new_h != term.win_height) {
      term.win_width  = new_w;
      term.win_height = new_h;
      int new_cols = (new_w - WIN_PADDING * 2) / (int)char_w;
      int new_rows = (new_h - WIN_PADDING * 2) / (int)char_h;
      if (new_cols < 1) new_cols = 1;
      if (new_rows < 1) new_rows = 1;
      pty_resize(master_fd, new_cols, new_rows);
    }

    // Parser stuck detection
    {
      parser_state_t cur = term.parser.state;
      if (cur != PARSER_GROUND) {
        if (cur == prev_parser_state) {
          parser_stuck_frames++;
          if (parser_stuck_frames >= STUCK_FRAME_LIMIT) {
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
    BeginDrawing();
    ClearBackground((Color){ 15, 20, 25, 255 });

    int screen_rows = term.screen.rows;
    int sb_count = scrollback_count(term.scrollback);

    if (term.scroll_offset > 0) {
      // ── Scrollback (viewport) mode ──────────────────────────────
      // The viewport is shifted up by scroll_offset lines.
      // Top portion: scrollback lines (old history).
      // Bottom portion: the current visible screen (shifted up).
      char line[COLS];
      for (int r = 0; r < screen_rows; r++) {
        float y = (float)WIN_PADDING + (float)r * char_h;

        // Index into the scrollback buffer for this row.
        // When r=0 we show the oldest scrollback line of the viewport.
        // When r=scroll_offset-1 we show the newest scrollback line.
        int sb_idx = term.scroll_offset - 1 - r;

        if (sb_idx >= 0 && sb_idx < sb_count) {
          // Render from scrollback buffer (char-only, default colours)
          if (scrollback_get(term.scrollback, sb_idx, line, COLS)) {
            for (int c = 0; c < term.screen.cols; c++) {
              float x = (float)WIN_PADDING + (float)c * char_w;
              screen_cell_t sc;
              sc.ch = (uint8_t)line[c];
              sc.fg[0] = default_fg[0]; sc.fg[1] = default_fg[1];
              sc.fg[2] = default_fg[2];
              sc.bg[0] = default_bg[0]; sc.bg[1] = default_bg[1];
              sc.bg[2] = default_bg[2];
              sc.bold = 0;
              sc.underline = 0;
              render_cell(term.font, &sc, x, y, char_w, char_h);
            }
          }
        } else {
          // Scrollback lines exhausted – show the corresponding screen line
          int screen_row = r - term.scroll_offset;
          if (screen_row >= 0 && screen_row < term.screen.rows) {
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

      // Scrollback indicator bar at the bottom
      {
        int indicator_y = WIN_PADDING + screen_rows * (int)char_h;
        if (indicator_y + 2 < term.win_height) {
          // Semi-transparent bar
          DrawRectangle(0, indicator_y, term.win_width, 20,
                        (Color){ 30, 40, 60, 220 });

          char indicator[64];
          int len = snprintf(indicator, sizeof(indicator),
                             " -- Scrollback (%d/%d) --",
                             term.scroll_offset, sb_count);
          // Simple bitmap-font rendering using DrawText (raylib default font)
          DrawText(indicator, WIN_PADDING, indicator_y + 2, 14,
                   (Color){ 180, 200, 230, 255 });
          (void)len;
        }
      }
    } else {
      // ── Normal terminal mode ────────────────────────────────────
      for (int r = 0; r < term.screen.rows; r++) {
        float y = (float)WIN_PADDING + (float)r * char_h;
        for (int c = 0; c < term.screen.cols; c++) {
          const screen_cell_t *cell =
            &term.screen.cells[r * term.screen.cols + c];
          float x = (float)WIN_PADDING + (float)c * char_w;
          render_cell(term.font, cell, x, y, char_w, char_h);
        }
      }

      // Cursor: inverted block (only in normal mode)
      {
        float cx = (float)WIN_PADDING + (float)term.cx * char_w;
        float cy = (float)WIN_PADDING + (float)term.cy * char_h;
        DrawRectangle((int)cx, (int)cy, (int)char_w, (int)char_h,
                      (Color){ 220, 220, 220, 180 });

        int cp = term.screen.cells[term.cy * term.screen.cols + term.cx].ch;
        if (cp < 0x20) cp = ' ';
        DrawTextCodepoints(*font->current, &cp, 1,
                           (Vector2){ cx, cy + font->ascent },
                           font->font_size, font->spacing,
                           (Color){ 15, 20, 25, 255 });
      }
    }

    EndDrawing();
  }

done:
  close(master_fd);
  scrollback_destroy(term.scrollback);
  screen_buf_free(&term.screen);
  font_uninit(font);
  CloseWindow();
  return 0;
}
