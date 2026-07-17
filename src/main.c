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
#include "scrollback.h"

#define ROWS 36
#define COLS 100
#define FONT_SIZE 20
#define WIN_PADDING 10
#define CHAR_WIDTH  10   // approximate character width at FONT_SIZE

// Window dimensions derived from the terminal grid so all rows/cols are visible.
#define WIN_WIDTH  (COLS * CHAR_WIDTH  + WIN_PADDING * 2)
#define WIN_HEIGHT (ROWS * FONT_SIZE   + WIN_PADDING * 2)

// ---------------------------------------------------------------------------
// Terminal state
// ---------------------------------------------------------------------------

typedef struct {
  char screen[ROWS][COLS];
  int cx, cy;               // cursor position
  int saved_cx, saved_cy;   // saved cursor position (DECSC/DECRC)
  parser_t parser;
  scrollback_t *scrollback; // scrollback buffer
  int scroll_offset;        // lines scrolled back (0 = normal view)
} terminal_t;

// ---------------------------------------------------------------------------
// Min / max helpers
// ---------------------------------------------------------------------------

static int min_int(int a, int b) { return a < b ? a : b; }
static int max_int(int a, int b) { return a > b ? a : b; }

// ---------------------------------------------------------------------------
// Screen helpers
// ---------------------------------------------------------------------------

static void scroll_up(terminal_t *t) {
  // Push the top line to scrollback before it scrolls off.
  if (t->scrollback)
    scrollback_push(t->scrollback, t->screen[0]);
  for (int i = 0; i < ROWS - 1; i++)
    memcpy(t->screen[i], t->screen[i + 1], COLS);
  memset(t->screen[ROWS - 1], ' ', COLS);
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
      scroll_up(t);
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
      scroll_up(t);
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
      clear_screen(t->screen);
      break;
    case 3: // erase entire screen + scrollback
      clear_screen(t->screen);
      if (t->scrollback)
        scrollback_clear(t->scrollback);
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
        scroll_up(t);
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
        scroll_up(t);
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
      if (t->scrollback)
        scrollback_reset(t->scrollback);
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
// Scroll key handling
// ---------------------------------------------------------------------------

/**
 * Check for and handle scroll-back keys using raylib's IsKeyPressed().
 * IsKeyPressed does NOT consume events from the GetKeyPressed queue, so
 * process_keyboard_input will still see these keys – we must skip them there.
 * Returns true if any scroll action was taken.
 */
static bool handle_scroll_keys(terminal_t *t) {
  if (!t)
    return false;

  bool scrolled = false;
  int sb_count = scrollback_count(t->scrollback);

  // Page Up   – scroll up one full screen (capped at available history)
  if (IsKeyPressed(KEY_PAGE_UP)) {
    t->scroll_offset = min_int(t->scroll_offset + ROWS, sb_count);
    scrolled = true;
  }

  // Page Down – scroll down one full screen
  if (IsKeyPressed(KEY_PAGE_DOWN)) {
    t->scroll_offset = max_int(0, t->scroll_offset - ROWS);
    scrolled = true;
  }

  // Shift + Up   – scroll up one line
  if (IsKeyPressed(KEY_UP) &&
      (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))) {
    t->scroll_offset = min_int(t->scroll_offset + 1, sb_count);
    scrolled = true;
  }

  // Shift + Down – scroll down one line
  if (IsKeyPressed(KEY_DOWN) &&
      (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))) {
    t->scroll_offset = max_int(0, t->scroll_offset - 1);
    scrolled = true;
  }

  // Mouse wheel
  float wheel = GetMouseWheelMove();
  if (wheel > 0) {
    t->scroll_offset = min_int(t->scroll_offset + (int)wheel, sb_count);
    scrolled = true;
  } else if (wheel < 0) {
    t->scroll_offset = max_int(0, t->scroll_offset + (int)wheel);
    scrolled = true;
  }

  return scrolled;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  InitWindow(WIN_WIDTH, WIN_HEIGHT, "dicTerm");

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

  // Create scrollback buffer.
  term.scrollback = scrollback_create(SCROLLBACK_CAPACITY, COLS);
  term.scroll_offset = 0;

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

    // Handle scroll-back keys (uses IsKeyPressed – does not consume events).
    handle_scroll_keys(&term);

    // Process raylib keyboard input and forward to the child.
    // Scroll keys (PAGE_UP, PAGE_DOWN, Shift+UP, Shift+DOWN) are skipped
    // internally by process_keyboard_input and are NOT forwarded to the PTY.
    // If any bytes were forwarded (written > 0), the user pressed a non-scroll
    // key, so we reset the scroll offset back to normal view.
    int written = process_keyboard_input(master_fd);
    if (written < 0) {
      perror("write(master_fd)");
      // Non-fatal; keep running.
    } else if (written > 0 && term.scroll_offset > 0) {
      term.scroll_offset = 0;
    }

    BeginDrawing();
    ClearBackground(RAYWHITE);

    // Number of scrollback lines visible at the top of the viewport.
    int sb_count = scrollback_count(term.scrollback);
    int sb_visible = min_int(term.scroll_offset, sb_count);

    for (int r = 0; r < ROWS; r++) {
      char line[COLS + 1];
      memset(line, ' ', COLS);
      line[COLS] = '\0';

      if (r < sb_visible) {
        // Show a line from the scrollback buffer.
        // The most recent scrollback lines appear closest to the active screen.
        int sb_index = sb_visible - 1 - r;
        scrollback_get(term.scrollback, sb_index, line, COLS);
      } else {
        // Show a line from the active screen buffer.
        int screen_row = r - sb_visible;
        if (screen_row >= 0 && screen_row < ROWS)
          memcpy(line, term.screen[screen_row], COLS);
      }

      // Trim trailing spaces for display.
      int len = COLS;
      while (len > 0 && line[len - 1] == ' ')
        len--;
      line[len] = '\0';

      DrawText(line, WIN_PADDING, WIN_PADDING + r * FONT_SIZE, FONT_SIZE, BLACK);
    }


    // Draw block cursor at (cx + 1, cy) - draws after the current character position
    if (term.cx < COLS && term.cy >= 0 && term.cy < ROWS) {
      int y_offset = WIN_PADDING + term.cy * FONT_SIZE;
      
      // cursor_x+1: Draw after the last text column, at the next position
      int cursor_x = WIN_PADDING + (term.cx + 1) * CHAR_WIDTH;
      int cursor_y = y_offset;
      int cursor_w = CHAR_WIDTH;
      int cursor_h = FONT_SIZE - 2;  // slightly less than full text height
      
      DrawRectangle(cursor_x, cursor_y, cursor_w, cursor_h, BLACK);
    }

    // Draw scroll indicator in top-right corner (if scrolled back).
    if (sb_visible > 0) {
      char indicator[32];
      snprintf(indicator, sizeof(indicator), "(-%d)", sb_visible);
      DrawText(indicator, WIN_WIDTH - 100, WIN_PADDING, FONT_SIZE, GRAY);
    }

    EndDrawing();
  }

done:
  scrollback_destroy(term.scrollback);
  CloseWindow();
  return 0;
}
