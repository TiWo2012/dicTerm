#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <raylib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "input.h"
#include "parser.h"

#define ROWS 36
#define COLS 100

// ---------------------------------------------------------------------------
// Terminal state
// ---------------------------------------------------------------------------

typedef struct {
  char screen[ROWS][COLS];
  int cx, cy;        // cursor position
  int saved_cx, saved_cy; // saved cursor position (DECSC/DECRC)
  parser_t parser;
} terminal_t;



// ---------------------------------------------------------------------------
// Screen helpers
// ---------------------------------------------------------------------------

static void scroll_buffer(char screen[ROWS][COLS]) {
  for (int i = 0; i < ROWS - 1; i++)
    memcpy(screen[i], screen[i + 1], COLS);
  memset(screen[ROWS - 1], ' ', COLS);
}

static void clear_screen(char screen[ROWS][COLS]) {
  for (int r = 0; r < ROWS; r++)
    memset(screen[r], ' ', COLS);
}

static void clear_line(char screen[ROWS][COLS], int row, int col_start) {
  if (row >= 0 && row < ROWS)
    memset(screen[row] + col_start, ' ', (size_t)(COLS - col_start));
}

static void clear_line_all(char screen[ROWS][COLS], int row) {
  if (row >= 0 && row < ROWS)
    memset(screen[row], ' ', COLS);
}

// ---------------------------------------------------------------------------
// Clamp cursor to bounds (without scrolling)
// ---------------------------------------------------------------------------

static void clamp_cursor(terminal_t *t) {
  if (t->cx < 0)  t->cx = 0;
  if (t->cx >= COLS) t->cx = COLS - 1;
  if (t->cy < 0)  t->cy = 0;
  if (t->cy >= ROWS) t->cy = ROWS - 1;
}

// ---------------------------------------------------------------------------
// Parser callbacks
// ---------------------------------------------------------------------------

static void on_print(char ch, void *ctx) {
  terminal_t *t = (terminal_t *)ctx;
  t->screen[t->cy][t->cx] = ch;
  t->cx++;
  if (t->cx >= COLS) {
    t->cx = 0;
    t->cy++;
    if (t->cy >= ROWS) {
      scroll_buffer(t->screen);
      t->cy = ROWS - 1;
    }
  }
}

static void on_execute(char c0, void *ctx) {
  terminal_t *t = (terminal_t *)ctx;

  switch (c0) {
  case '\n': // LF – move cursor down, preserve column position
    t->cy++;
    if (t->cy >= ROWS) {
      scroll_buffer(t->screen);
      t->cy = ROWS - 1;
    }
    break;
  case '\r': // CR
    t->cx = 0;
    break;
  case '\t': // HT
    do {
      t->cx++;
    } while (t->cx < COLS && (t->cx % 8) != 0);
    break;
  case '\b': // BS
    if (t->cx > 0)
      t->cx--;
    break;
  case '\a': // BEL – ignore (no audio yet)
  case '\v': // VT
  case '\f': // FF
  default:
    break;
  }
}

static void on_csi(int params[PARSER_MAX_PARAMS], int num_params,
                   char intermediates[PARSER_MAX_INTERMEDIATES],
                   int num_intermediates, char final, void *ctx) {
  (void)intermediates;
  (void)num_intermediates;
  terminal_t *t = (terminal_t *)ctx;

  // Helper to read a parameter with a default value.
  // Parameter value of -1 means "omitted", use default.
#define PARAM(n) ((n) < num_params && params[(n)] >= 0 ? params[(n)] : -1)

  switch (final) {
  case 'A': { // CUU – cursor up
    int n = PARAM(0);
    if (n < 0) n = 1;
    t->cy -= n;
    clamp_cursor(t);
    break;
  }
  case 'B': { // CUD – cursor down
    int n = PARAM(0);
    if (n < 0) n = 1;
    t->cy += n;
    clamp_cursor(t);
    break;
  }
  case 'C': { // CUF – cursor forward
    int n = PARAM(0);
    if (n < 0) n = 1;
    t->cx += n;
    clamp_cursor(t);
    break;
  }
  case 'D': { // CUB – cursor back
    int n = PARAM(0);
    if (n < 0) n = 1;
    t->cx -= n;
    clamp_cursor(t);
    break;
  }
  case 'H':   // CUP – cursor position
  case 'f': { // HVP – horizontal vertical position
    int row = PARAM(0);
    int col = PARAM(1);
    if (row < 0) row = 1;
    if (col < 0) col = 1;
    t->cy = row - 1;
    t->cx = col - 1;
    clamp_cursor(t);
    break;
  }
  case 'J': { // ED – erase in display
    int mode = PARAM(0);
    if (mode < 0) mode = 0;
    switch (mode) {
    case 0: // erase from cursor to end of screen
      clear_line(t->screen, t->cy, t->cx);
      for (int r = t->cy + 1; r < ROWS; r++)
        memset(t->screen[r], ' ', COLS);
      break;
    case 1: // erase from start to cursor
      clear_line(t->screen, t->cy, 0);
      for (int r = 0; r < t->cy; r++)
        memset(t->screen[r], ' ', COLS);
      // Don't erase the character at cursor
      break;
    case 2: // erase entire screen
    case 3: // erase entire screen + scrollback (treat same as 2)
      clear_screen(t->screen);
      break;
    }
    break;
  }
  case 'K': { // EL – erase in line
    int mode = PARAM(0);
    if (mode < 0) mode = 0;
    switch (mode) {
    case 0: // erase from cursor to end of line
      clear_line(t->screen, t->cy, t->cx);
      break;
    case 1: // erase from start of line to cursor
      memset(t->screen[t->cy], ' ', (size_t)(t->cx + 1));
      break;
    case 2: // erase entire line
      clear_line_all(t->screen, t->cy);
      break;
    }
    break;
  }
  case 'm': // SGR – select graphic rendition (ignore attributes for now)
    break;
  case 's': // save cursor (SCP)
    t->saved_cx = t->cx;
    t->saved_cy = t->cy;
    break;
  case 'u': // restore cursor (RCP)
    t->cx = t->saved_cx;
    t->cy = t->saved_cy;
    clamp_cursor(t);
    break;
  default:
    break;
  }

#undef PARAM
}

static void on_esc(char intermediates[PARSER_MAX_INTERMEDIATES],
                   int num_intermediates, char final, void *ctx) {
  (void)intermediates;
  terminal_t *t = (terminal_t *)ctx;

  if (num_intermediates == 0) {
    switch (final) {
    case 'D': // IND – index (cursor down + scroll)
      t->cy++;
      if (t->cy >= ROWS) {
        scroll_buffer(t->screen);
        t->cy = ROWS - 1;
      }
      break;
    case 'M': // RI – reverse index (cursor up + scroll)
      t->cy--;
      if (t->cy < 0) {
        // Scroll the screen buffer down (insert blank line at top)
        for (int r = ROWS - 1; r > 0; r--)
          memcpy(t->screen[r], t->screen[r - 1], COLS);
        memset(t->screen[0], ' ', COLS);
        t->cy = 0;
      }
      break;
    case 'E': // NEL – next line (CR + LF)
      t->cx = 0;
      t->cy++;
      if (t->cy >= ROWS) {
        scroll_buffer(t->screen);
        t->cy = ROWS - 1;
      }
      break;
    case '7': // DECSC – save cursor
      t->saved_cx = t->cx;
      t->saved_cy = t->cy;
      break;
    case '8': // DECRC – restore cursor
      t->cx = t->saved_cx;
      t->cy = t->saved_cy;
      clamp_cursor(t);
      break;
    case 'c': // RIS – reset to initial state
      clear_screen(t->screen);
      t->cx = 0;
      t->cy = 0;
      break;
    default:
      break;
    }
  }
}

static void on_osc(int command, const char *str, void *ctx) {
  (void)command;
  (void)str;
  (void)ctx;
  // OSC commands (e.g. set window title) – ignore for now.
}

static void on_dcs(int command, const char *str, void *ctx) {
  (void)command;
  (void)str;
  (void)ctx;
}

static void on_string(const char *str, void *ctx) {
  (void)str;
  (void)ctx;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  InitWindow(800, 600, "dicTerm");

  int master_fd;
  pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
  if (pid == -1) {
    perror("forkpty");
    return 1;
  }

  if (fcntl(master_fd, F_SETFL, O_NONBLOCK) == -1) {
    perror("fcntl");
    // Non-fatal; continue without non-blocking mode.
  }

  if (pid == 0) {
    execl("/bin/bash", "bash", NULL);
    perror("execl");
    _exit(1);
  }

  SetTargetFPS(60);

  // Initialise terminal state.
  terminal_t term;
  memset(&term, 0, sizeof(term));
  for (int r = 0; r < ROWS; r++)
    memset(term.screen[r], ' ', COLS);

  // Set up parser callbacks.
  parser_callbacks_t cbs = {
      .on_print   = on_print,
      .on_execute = on_execute,
      .on_csi     = on_csi,
      .on_esc     = on_esc,
      .on_osc     = on_osc,
      .on_dcs     = on_dcs,
      .on_string  = on_string,
  };
  parser_init(&term.parser, &cbs, &term);

  while (!WindowShouldClose()) {
    // Read PTY output (non-blocking) and feed it into the escape-sequence
    // parser.  We drain the fd so the shell doesn't stall on writes.
    for (;;) {
      char buf[65536];
      ssize_t n = read(master_fd, buf, sizeof(buf));
      if (n > 0) {
        parser_feed(&term.parser, (const uint8_t *)buf, (size_t)n);
      } else if (n == 0) {
        // PTY closed – child exited.
        goto done;
      } else {
        // EAGAIN / EWOULDBLOCK means no data right now – move on.
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          break;
        // Real error.
        perror("read(master_fd)");
        goto done;
      }
    }

    // Process raylib keyboard input and forward to the child.
    if (process_keyboard_input(master_fd) < 0) {
      perror("write(master_fd)");
      // Non-fatal; keep running.
    }

    BeginDrawing();
    ClearBackground(RAYWHITE);

    for (int r = 0; r < ROWS; r++) {
      char line[COLS + 1];
      memcpy(line, term.screen[r], COLS);
      line[COLS] = '\0';

      // Trim trailing spaces for display.
      int len = COLS;
      while (len > 0 && line[len - 1] == ' ')
        len--;
      line[len] = '\0';

      DrawText(line, 10, 10 + r * 20, 20, BLACK);
    }

    EndDrawing();
  }

done:
  CloseWindow();
  return 0;
}
