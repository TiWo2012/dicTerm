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

// ---------------------------------------------------------------------------
// Terminal state
// ---------------------------------------------------------------------------

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

static void scroll_up(terminal_t *t, int count) {
  int cols = t->screen.cols;
  char line_buf[COLS];
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
// Screen helpers
// ---------------------------------------------------------------------------

static void clear_screen(terminal_t *t) {
  for (int r = 0; r < t->screen.rows; r++)
    screen_buf_clear_row(&t->screen, r, true);
  scrollback_clear(t->scrollback);
  t->cx = 0;
  t->cy = 0;
}

// ---------------------------------------------------------------------------
// Parser callbacks
// ---------------------------------------------------------------------------

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
// SGR colour handling
// ---------------------------------------------------------------------------

static void reset_sgr(terminal_t *t) {
  t->cur_fg[0] = default_fg[0]; t->cur_fg[1] = default_fg[1]; t->cur_fg[2] = default_fg[2];
  t->cur_bg[0] = default_bg[0]; t->cur_bg[1] = default_bg[1]; t->cur_bg[2] = default_bg[2];
  t->cur_bold = false;
  t->cur_underline = false;
}

static void set_fg(terminal_t *t, int idx) {
  if (idx >= 0 && idx < 8) {
    t->cur_fg[0] = ansi_std[idx][0];
    t->cur_fg[1] = ansi_std[idx][1];
    t->cur_fg[2] = ansi_std[idx][2];
  }
}

static void set_bg(terminal_t *t, int idx) {
  if (idx >= 0 && idx < 8) {
    t->cur_bg[0] = ansi_std[idx][0];
    t->cur_bg[1] = ansi_std[idx][1];
    t->cur_bg[2] = ansi_std[idx][2];
  }
}

static void set_bright_fg(terminal_t *t, int idx) {
  if (idx >= 0 && idx < 8) {
    t->cur_fg[0] = ansi_bright[idx][0];
    t->cur_fg[1] = ansi_bright[idx][1];
    t->cur_fg[2] = ansi_bright[idx][2];
  }
}

static void set_bright_bg(terminal_t *t, int idx) {
  if (idx >= 0 && idx < 8) {
    t->cur_bg[0] = ansi_bright[idx][0];
    t->cur_bg[1] = ansi_bright[idx][1];
    t->cur_bg[2] = ansi_bright[idx][2];
  }
}

// ---------------------------------------------------------------------------
// CSI handler
// ---------------------------------------------------------------------------

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
      // No params – treat as single param 0 (reset)
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
      } else if (p >= 30 && p <= 37) {
        set_fg(t, p - 30);
      } else if (p >= 40 && p <= 47) {
        set_bg(t, p - 40);
      } else if (p >= 90 && p <= 97) {
        set_bright_fg(t, p - 90);
      } else if (p >= 100 && p <= 107) {
        set_bright_bg(t, p - 100);
      } else if (p == 38) {
        // Extended foreground colour – not fully supported yet
      } else if (p == 48) {
        // Extended background colour – not fully supported yet
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

static void on_osc(int cmd, const char *str, void *ctx) {
  (void)cmd; (void)str; (void)ctx;
}

static void on_dcs(int cmd, const char *str, void *ctx) {
  (void)cmd; (void)str; (void)ctx;
}

// ---------------------------------------------------------------------------
// PTY helpers
// ---------------------------------------------------------------------------

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

    // Keyboard input → PTY
    process_keyboard_input(master_fd);

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

    for (int r = 0; r < term.screen.rows; r++) {
      float y = (float)WIN_PADDING + (float)r * char_h;
      for (int c = 0; c < term.screen.cols; c++) {
        const screen_cell_t *cell = &term.screen.cells[r * term.screen.cols + c];
        float x = (float)WIN_PADDING + (float)c * char_w;
        render_cell(term.font, cell, x, y, char_w, char_h);
      }
    }

    // Cursor: inverted block
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
