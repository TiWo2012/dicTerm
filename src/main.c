/* dicTerm - GPU-accelerated terminal emulator */
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

#include "font.h"
#include "input.h"
#include "parser.h"
#include "scrollback.h"

#define ROWS 36
#define COLS 100
#define WIN_PADDING 10
#define WIN_WIDTH  1280
#define WIN_HEIGHT 800

// ---------------------------------------------------------------------------
// Terminal state
// ---------------------------------------------------------------------------
typedef struct {
  char       screen[ROWS][COLS];
  int        cx, cy;
  int        saved_cx, saved_cy;
  parser_t   parser;
  scrollback_t *scrollback;
  font_handle_t *font;
  int        win_width;
  int        win_height;
} terminal_t;

// ---------------------------------------------------------------------------
// Screen helpers
// ---------------------------------------------------------------------------
static void scroll_up(terminal_t *t) {
  scrollback_push(t->scrollback, t->screen[0]);
  for (int i = 0; i < ROWS - 1; i++)
    memcpy(t->screen[i], t->screen[i + 1], COLS);
  memset(t->screen[ROWS - 1], ' ', COLS);
}

static void clear_screen(terminal_t *t) {
  for (int r = 0; r < ROWS; r++)
    memset(t->screen[r], ' ', COLS);
  scrollback_clear(t->scrollback);
  t->cx = 0;
  t->cy = 0;
}

static void erase_display(terminal_t *t, int mode) {
  if (mode == 0) {
    for (int r = t->cy; r < ROWS; r++) {
      int start = (r == t->cy) ? t->cx : 0;
      for (int c = start; c < COLS; c++)
        t->screen[r][c] = ' ';
    }
  } else if (mode == 1) {
    for (int r = 0; r <= t->cy; r++) {
      int end = (r == t->cy) ? t->cx : COLS - 1;
      for (int c = 0; c <= end; c++)
        t->screen[r][c] = ' ';
    }
  } else if (mode == 2) {
    clear_screen(t);
  }
}

static void erase_line(terminal_t *t, int mode) {
  if (mode == 0) {
    for (int c = t->cx; c < COLS; c++)
      t->screen[t->cy][c] = ' ';
  } else if (mode == 1) {
    for (int c = 0; c <= t->cx; c++)
      t->screen[t->cy][c] = ' ';
  } else if (mode == 2) {
    memset(t->screen[t->cy], ' ', COLS);
  }
}

// ---------------------------------------------------------------------------
// Parser callbacks
// ---------------------------------------------------------------------------
static void on_print(char ch, void *ctx) {
  terminal_t *t = (terminal_t *)ctx;
  if (t->cx >= COLS || t->cy >= ROWS) return;
  t->screen[t->cy][t->cx++] = ch;
  if (t->cx >= COLS) {
    t->cx = 0;
    if (++t->cy >= ROWS) {
      scroll_up(t);
      t->cy = ROWS - 1;
    }
  }
}

static void on_execute(char c0, void *ctx) {
  terminal_t *t = (terminal_t *)ctx;
  switch (c0) {
  case '\n':
    t->cy++;
    if (t->cy >= ROWS) { scroll_up(t); t->cy = ROWS - 1; }
    break;
  case '\r': t->cx = 0; break;
  case '\b': if (t->cx > 0) t->cx--; break;
  case '\t':
    t->cx += 8;
    if (t->cx >= COLS) t->cx = COLS - 1;
    break;
  case 0x0B: case 0x0C:
    t->cy++;
    if (t->cy >= ROWS) { scroll_up(t); t->cy = ROWS - 1; }
    break;
  }
}

static void on_csi(int params[16], int num_params,
                   char intermediates[2], int num_intermediates,
                   char final, void *ctx) {
  (void)intermediates; (void)num_intermediates;
  terminal_t *t = (terminal_t *)ctx;

#define PARAM(n) ((n) < num_params && params[(n)] >= 0 ? params[(n)] : -1)

  switch (final) {
  case 'A': { int n = PARAM(0); if (n < 0) n = 1; t->cy -= n; if (t->cy < 0) t->cy = 0; break; }
  case 'B': { int n = PARAM(0); if (n < 0) n = 1; t->cy += n; if (t->cy >= ROWS) t->cy = ROWS - 1; break; }
  case 'C': { int n = PARAM(0); if (n < 0) n = 1; t->cx += n; if (t->cx >= COLS) t->cx = COLS - 1; break; }
  case 'D': { int n = PARAM(0); if (n < 0) n = 1; t->cx -= n; if (t->cx < 0) t->cx = 0; break; }
  case 'H':
  case 'f': {
    int row = PARAM(0); if (row < 0) row = 1;
    int col = PARAM(1); if (col < 0) col = 1;
    t->cy = (row > 0) ? row - 1 : 0;
    t->cx = (col > 0) ? col - 1 : 0;
    if (t->cy >= ROWS) t->cy = ROWS - 1;
    if (t->cx >= COLS) t->cx = COLS - 1;
    break;
  }
  case 'J': {
    int mode = PARAM(0);
    if (mode < 0) mode = 0;
    erase_display(t, mode);
    break;
  }
  case 'K': {
    int mode = PARAM(0);
    if (mode < 0) mode = 0;
    erase_line(t, mode);
    break;
  }
  case 's': t->saved_cx = t->cx; t->saved_cy = t->cy; break;
  case 'u': t->cx = t->saved_cx; t->cy = t->saved_cy; break;
  case 'm': break;
  }

#undef PARAM
}

static void on_esc(char intermediates[2], int num_intermediates,
                   char final, void *ctx) {
  (void)intermediates; (void)num_intermediates;
  terminal_t *t = (terminal_t *)ctx;
  switch (final) {
  case '7': t->saved_cx = t->cx; t->saved_cy = t->cy; break;
  case '8': t->cx = t->saved_cx; t->cy = t->saved_cy; break;
  case 'D':
    t->cy++;
    if (t->cy >= ROWS) { scroll_up(t); t->cy = ROWS - 1; }
    break;
  case 'M':
    t->cy--;
    if (t->cy < 0) {
      for (int i = ROWS - 1; i > 0; i--)
        memcpy(t->screen[i], t->screen[i - 1], COLS);
      memset(t->screen[0], ' ', COLS);
      t->cy = 0;
    }
    break;
  case 'E':
    t->cx = 0; t->cy++;
    if (t->cy >= ROWS) { scroll_up(t); t->cy = ROWS - 1; }
    break;
  case 'c': clear_screen(t); break;
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
// Main
// ---------------------------------------------------------------------------
int main(void) {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(WIN_WIDTH, WIN_HEIGHT, "dicTerm");

  // Font subsystem (requires GPU context)
  font_handle_t *font = font_init(NULL, NULL, 20.0f);
  if (!font) {
    fprintf(stderr, "dicTerm: Failed to initialize fonts.\n");
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
  term.scrollback = scrollback_create(SCROLLBACK_CAPACITY, COLS);
  if (!term.scrollback) {
    close(master_fd);
    font_uninit(font);
    CloseWindow();
    return 1;
  }
  term.font = font;
  term.win_width  = WIN_WIDTH;
  term.win_height = WIN_HEIGHT;

  for (int r = 0; r < ROWS; r++)
    memset(term.screen[r], ' ', COLS);

  parser_callbacks_t cbs = {
    .on_print   = on_print,
    .on_execute = on_execute,
    .on_csi     = on_csi,
    .on_esc     = on_esc,
    .on_osc     = on_osc,
    .on_dcs     = on_dcs,
  };
  parser_init(&term.parser, &cbs, &term);

  // Parser stuck detection: if the parser stays in the same non-GROUND
  // state for more than STUCK_FRAME_LIMIT frames, forcibly reset it.
  // This prevents unterminated OSC/DCS/CSI sequences from paralyzing
  // the terminal (e.g. zsh window-title OSC without a terminator).
  enum { STUCK_FRAME_LIMIT = 60 }; // ~1 second at 60 FPS
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

    // Parser stuck detection: if the parser hasn't returned to GROUND
    // for STUCK_FRAME_LIMIT frames, forcibly reset it.
    // This is essential because some shells (zsh) may send OSC sequences
    // (e.g. \e]0;... for window title) without proper BEL/ST terminators,
    // which would otherwise permanently paralyze the terminal.
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
          // State changed but still not GROUND – reset the counter
          parser_stuck_frames = 0;
        }
      } else {
        parser_stuck_frames = 0;
      }
      prev_parser_state = cur;
    }

    // Render
    BeginDrawing();
    ClearBackground((Color){ 30, 30, 30, 255 });

    Color fg = (Color){ 220, 220, 220, 255 };
    Color bg = (Color){ 30, 30, 30, 0 };

    for (int r = 0; r < ROWS; r++) {
      char line[COLS + 1];
      memcpy(line, term.screen[r], COLS);
      line[COLS] = '\0';
      float y = (float)WIN_PADDING + (float)r * char_h;
      font_render_line(font, line, COLS, (float)WIN_PADDING, y, fg, bg);
    }

    // Cursor: inverted block
    {
      float cx = (float)WIN_PADDING + (float)term.cx * char_w;
      float cy = (float)WIN_PADDING + (float)term.cy * char_h;
      DrawRectangle((int)cx, (int)cy, (int)char_w, (int)char_h,
                    (Color){ 220, 220, 220, 180 });

      char cc = term.screen[term.cy][term.cx];
      if (cc < 0x20) cc = ' ';
      int cp[1] = { (int)(unsigned char)cc };
      DrawTextCodepoints(*font->current, cp, 1,
                         (Vector2){ cx, cy + font->ascent },
                         font->font_size, font->spacing,
                         (Color){ 30, 30, 30, 255 });
    }

    EndDrawing();
  }

done:
  close(master_fd);
  scrollback_destroy(term.scrollback);
  font_uninit(font);
  CloseWindow();
  return 0;
}
