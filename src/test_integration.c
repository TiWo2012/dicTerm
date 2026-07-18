// ---------------------------------------------------------------------------
// Integration test suite for dicTerm.
//
// Tests the interaction between parser, terminal state, and scrollback
// buffer with realistic multi-sequence scenarios.  These tests exercise
// scenarios that main.c would encounter during normal operation.
//
// Build:  cc -std=c2y -Wall -Wextra -Werror -Iinclude
//             src/parser.c src/scrollback.c src/test_integration.c
//             -o test_integration
// Run:    ./test_integration
// ---------------------------------------------------------------------------

#include "parser.h"
#include "scrollback.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Terminal dimensions (must match main.c)
// ---------------------------------------------------------------------------

#define ROWS 36
#define COLS 100

// ---------------------------------------------------------------------------
// Test framework
// ---------------------------------------------------------------------------

static int tests_run = 0;
static int tests_failed = 0;
static int tests_passed = 0;

#define TEST(name)                                                        \
  do {                                                                    \
    tests_run++;                                                          \
    printf("  test: %-48s ", #name);                                      \
    if (!test_##name()) {                                                 \
      printf("FAIL\n");                                                   \
      tests_failed++;                                                     \
    } else {                                                              \
      printf("ok\n");                                                     \
      tests_passed++;                                                     \
    }                                                                     \
  } while (0)

#define ASSERT(cond, msg)                                                 \
  do {                                                                    \
    if (!(cond)) {                                                        \
      printf("\n    ASSERT: %s\n    File: %s:%d\n", msg, __FILE__,         \
             __LINE__);                                                   \
      return false;                                                       \
    }                                                                     \
  } while (0)

#define ASSERT_INT_EQ(a, b, msg)                                          \
  do {                                                                    \
    if ((a) != (b)) {                                                     \
      printf("\n    ASSERT_INT_EQ(%d == %d): %s\n    File: %s:%d\n",      \
             (int)(a), (int)(b), msg, __FILE__, __LINE__);                \
      return false;                                                       \
    }                                                                     \
  } while (0)

#define ASSERT_CHAR_EQ(a, b, msg)                                         \
  do {                                                                    \
    if ((a) != (b)) {                                                     \
      printf("\n    ASSERT_CHAR_EQ('%c' == '%c'): %s\n    File: %s:%d\n", \
             (char)(a), (char)(b), msg, __FILE__, __LINE__);              \
      return false;                                                       \
    }                                                                     \
  } while (0)

// ---------------------------------------------------------------------------
// Terminal state (same as test_terminal.c — mirrors main.c's logic)
// ---------------------------------------------------------------------------

typedef struct {
  char screen[ROWS][COLS];
  int cx, cy;
  int saved_cx, saved_cy;
  parser_t parser;
  scrollback_t *scrollback;
} terminal_t;

static void scroll_up(terminal_t *t) {
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

static void clamp_cursor(terminal_t *t) {
  if (t->cx < 0)  t->cx = 0;
  if (t->cx >= COLS) t->cx = COLS - 1;
  if (t->cy < 0)  t->cy = 0;
  if (t->cy >= ROWS) t->cy = ROWS - 1;
}

// ---------------------------------------------------------------------------
// Parser callbacks (same as main.c)
// ---------------------------------------------------------------------------

static void on_print(uint8_t ch, void *ctx) {
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
  case '\n':
    t->cy++;
    if (t->cy >= ROWS) {
      scroll_up(t);
      t->cy = ROWS - 1;
    }
    break;
  case '\r':
    t->cx = 0;
    break;
  case '\t':
    do { t->cx++; } while (t->cx < COLS && (t->cx % 8) != 0);
    if (t->cx >= COLS) t->cx = COLS - 1;
    break;
  case '\b':
    if (t->cx > 0) t->cx--;
    break;
  case '\a': case '\v': case '\f':
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

#define PARAM(n) ((n) < num_params && params[(n)] >= 0 ? params[(n)] : -1)

  switch (final) {
  case 'A': {
    int n = PARAM(0);
    if (n < 0) n = 1;
    t->cy -= n;
    clamp_cursor(t);
    break;
  }
  case 'B': {
    int n = PARAM(0);
    if (n < 0) n = 1;
    t->cy += n;
    clamp_cursor(t);
    break;
  }
  case 'C': {
    int n = PARAM(0);
    if (n < 0) n = 1;
    t->cx += n;
    clamp_cursor(t);
    break;
  }
  case 'D': {
    int n = PARAM(0);
    if (n < 0) n = 1;
    t->cx -= n;
    clamp_cursor(t);
    break;
  }
  case 'H':
  case 'f': {
    int row = PARAM(0);
    int col = PARAM(1);
    if (row < 0) row = 1;
    if (col < 0) col = 1;
    t->cy = row - 1;
    t->cx = col - 1;
    clamp_cursor(t);
    break;
  }
  case 'J': {
    int mode = PARAM(0);
    if (mode < 0) mode = 0;
    switch (mode) {
    case 0:
      clear_line(t->screen, t->cy, t->cx);
      for (int r = t->cy + 1; r < ROWS; r++)
        memset(t->screen[r], ' ', COLS);
      break;
    case 1:
      // Erase from start of display through the cursor position.
      memset(t->screen[t->cy], ' ', (size_t)(t->cx + 1));
      for (int r = 0; r < t->cy; r++)
        memset(t->screen[r], ' ', COLS);
      break;
    case 2:
      clear_screen(t->screen);
      break;
    case 3:
      clear_screen(t->screen);
      if (t->scrollback)
        scrollback_clear(t->scrollback);
      break;
    }
    break;
  }
  case 'K': {
    int mode = PARAM(0);
    if (mode < 0) mode = 0;
    switch (mode) {
    case 0:
      clear_line(t->screen, t->cy, t->cx);
      break;
    case 1:
      memset(t->screen[t->cy], ' ', (size_t)(t->cx + 1));
      break;
    case 2:
      clear_line_all(t->screen, t->cy);
      break;
    }
    break;
  }
  case 'm':
    break;
  case 's':
    t->saved_cx = t->cx;
    t->saved_cy = t->cy;
    break;
  case 'u':
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
    case 'D':
      t->cy++;
      if (t->cy >= ROWS) {
        scroll_up(t);
        t->cy = ROWS - 1;
      }
      break;
    case 'M':
      t->cy--;
      if (t->cy < 0) {
        for (int r = ROWS - 1; r > 0; r--)
          memcpy(t->screen[r], t->screen[r - 1], COLS);
        memset(t->screen[0], ' ', COLS);
        t->cy = 0;
      }
      break;
    case 'E':
      t->cx = 0;
      t->cy++;
      if (t->cy >= ROWS) {
        scroll_up(t);
        t->cy = ROWS - 1;
      }
      break;
    case '7':
      t->saved_cx = t->cx;
      t->saved_cy = t->cy;
      break;
    case '8':
      t->cx = t->saved_cx;
      t->cy = t->saved_cy;
      clamp_cursor(t);
      break;
    case 'c':
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
  (void)command; (void)str; (void)ctx;
}

static void on_dcs(int command, const char *str, void *ctx) {
  (void)command; (void)str; (void)ctx;
}

static void on_string(const char *str, void *ctx) {
  (void)str; (void)ctx;
}

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static parser_callbacks_t s_callbacks = {
    .on_print   = on_print,
    .on_execute = on_execute,
    .on_csi     = on_csi,
    .on_esc     = on_esc,
    .on_osc     = on_osc,
    .on_dcs     = on_dcs,
    .on_string  = on_string,
};

static void term_init(terminal_t *t, int sb_cap) {
  memset(t, 0, sizeof(*t));
  for (int r = 0; r < ROWS; r++)
    memset(t->screen[r], ' ', COLS);
  t->scrollback = (sb_cap > 0) ? scrollback_create(sb_cap, COLS) : NULL;
  parser_init(&t->parser, &s_callbacks, t);
}

static void term_destroy(terminal_t *t) {
  scrollback_destroy(t->scrollback);
  t->scrollback = NULL;
}

#define FEED(t, str) \
  parser_feed(&(t)->parser, (const uint8_t *)(str), sizeof(str) - 1)

#define ASSERT_CELL(t, row, col, ch, msg) \
  ASSERT_CHAR_EQ((t)->screen[(row)][(col)], (ch), msg)

/** Check that a row matches the expected string, space-padded. */
static bool assert_row(terminal_t *t, int row, const char *expected) {
  size_t len = strlen(expected);
  for (size_t i = 0; i < len && i < (size_t)COLS; i++) {
    if (t->screen[row][i] != expected[i]) {
      printf("\n    ASSERT_ROW: row %d col %zu: expected '%c' got '%c'\n",
             row, i, expected[i], t->screen[row][i]);
      return false;
    }
  }
  for (size_t i = len; i < (size_t)COLS; i++) {
    if (t->screen[row][i] != ' ') {
      printf("\n    ASSERT_ROW: row %d col %zu: expected ' ' got '%c'\n",
             row, i, t->screen[row][i]);
      return false;
    }
  }
  return true;
}

/**
 * Feed a complete shell-prompt-like sequence and check the final screen state.
 * This simulates what a real shell sends on startup, including:
 *   - erase screen + cursor home
 *   - SGR color codes (bold/green for user, reset, bold/blue for path, reset)
 *   - the prompt text itself
 *   - cursor positioning for the prompt
 */
static const char shell_prompt_sequence[] =
    "\x1b[2J"           // ED 2 – clear entire screen
    "\x1b[1;1H"         // CUP – home cursor
    "\x1b[1;32m"        // SGR bold/green
    "user@host"         // username@hostname
    "\x1b[0m"           // SGR reset
    ":"                 // colon separator
    "\x1b[1;34m"        // SGR bold/blue
    "/home/user"        // current directory
    "\x1b[0m"           // SGR reset
    "$ "                // prompt
    ;                   // cursor waits for input here

// ---------------------------------------------------------------------------
// Integration test cases
// ---------------------------------------------------------------------------

// ---- Shell prompt scenarios ----

static bool test_shell_prompt_full_sequence(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, shell_prompt_sequence);

  // After ED 2 + CUP, the prompt should start at (0,0).
  // Verify row 0 content (note: SGR codes are ignored, text passes through).
  ASSERT(assert_row(&t, 0, "user@host:/home/user$ "),
         "prompt row 0 correct");

  // All other rows should be empty (spaces).
  for (int r = 1; r < ROWS; r++)
    ASSERT_CELL(&t, r, 0, ' ', "rows 1+ blank after clear");

  // Cursor should be at row 0, col after "$ ".
  // "user@host:/home/user$ " = 22 chars (count them).
  ASSERT_INT_EQ(t.cx, 22, "cursor at col 22 after prompt");
  ASSERT_INT_EQ(t.cy, 0, "cursor at row 0 after prompt");

  term_destroy(&t);
  return true;
}

static bool test_shell_prompt_then_user_input(void) {
  terminal_t t;
  term_init(&t, 0);

  // 1. Shell sends prompt
  FEED(&t, shell_prompt_sequence);

  // 2. Simulate user typing a command (echoed by pty)
  FEED(&t, "ls -la");

  ASSERT(assert_row(&t, 0, "user@host:/home/user$ ls -la"),
         "prompt + typed command on row 0");

  // Prompt is 22 chars + "ls -la" is 6 chars = 28 chars, cx = 28.
  ASSERT_INT_EQ(t.cx, 28, "cursor after 'ls -la' (22 + 6)");
  ASSERT_INT_EQ(t.cy, 0, "cursor still row 0");

  term_destroy(&t);
  return true;
}

static bool test_command_output_multiple_lines(void) {
  terminal_t t;
  term_init(&t, 0);

  // 1. Prompt
  FEED(&t, shell_prompt_sequence);
  // 2. Typed command
  FEED(&t, "echo Hello World");
  // 3. LF (Enter) — command executes
  FEED(&t, "\n");
  // 4. Output from the command
  FEED(&t, "Hello World\n");
  // 5. Next prompt
  FEED(&t, shell_prompt_sequence);

  // Row 0: typed command (the shell typically echoes it on a new line
  //        after ENTER, but our model just prints it before the LF).
  // Actually, since we typed on row 0 and then LF scrolled, let's trace:
  //   - After prompt + "echo Hello World", we're at row 0, col ~34.
  //   - LF moves to row 1.
  //   - "Hello World\n" prints on row 1, then LF moves to row 2.
  //   - shell_prompt_sequence does ED 2 + home, so it clears everything
  //     and writes prompt at (0,0).
  // So result: row 0 has prompt, rows 1+ are blank.
  ASSERT(assert_row(&t, 0, "user@host:/home/user$ "),
         "prompt on row 0 after command output + new prompt");

  for (int r = 1; r < ROWS; r++)
    ASSERT_CELL(&t, r, 0, ' ', "rows below are blank");

  term_destroy(&t);
  return true;
}

static bool test_output_without_clear(void) {
  terminal_t t;
  term_init(&t, 0);

  // Use \r\n so each line starts at column 0.
  FEED(&t, "Some previous output\r\n");
  FEED(&t, "More output\r\n");
  FEED(&t, "user@host:~$ ");

  ASSERT(assert_row(&t, 0, "Some previous output"),
         "row 0 is first output line");
  ASSERT(assert_row(&t, 1, "More output"),
         "row 1 is second output line");
  ASSERT(assert_row(&t, 2, "user@host:~$ "),
         "row 2 is prompt");

  term_destroy(&t);
  return true;
}

// ---- Complex cursor movement ----

static bool test_cursor_movement_overlay(void) {
  terminal_t t;
  term_init(&t, 0);

  // Write lines and then go back to overlay something.  Use \r\n so each
  // line starts at column 0.
  FEED(&t, "First line here\r\n");
  FEED(&t, "Second line here\r\n");
  FEED(&t, "Third line here");

  // CUP to row 2 col 8 (1-indexed) => (1,7) 0-indexed.
  // This is the 'l' in "line".  Overwrite with "X", then check cells.
  FEED(&t, "\x1b[2;8H");
  FEED(&t, "X");

  ASSERT(assert_row(&t, 0, "First line here"), "row 0 unchanged");
  // Row 1: "Second line here".  Col 0='S', col 7 should now be 'X', col 8='i'.
  ASSERT_CELL(&t, 1, 0, 'S', "row 1 col 0 = 'S'");
  ASSERT_CELL(&t, 1, 7, 'X', "row 1 col 7 = 'X' (overlay)");
  ASSERT_CELL(&t, 1, 8, 'i', "row 1 col 8 = 'i' (preserved)");
  ASSERT(assert_row(&t, 2, "Third line here"), "row 2 unchanged");

  term_destroy(&t);
  return true;
}

static bool test_rapid_cursor_and_print(void) {
  terminal_t t;
  term_init(&t, 0);

  // Rapidly alternate cursor position and print.
  // Each string is a string literal so sizeof works in FEED.
  FEED(&t, "\x1b[5;10HAAA");
  FEED(&t, "\x1b[3;20HBBB");
  FEED(&t, "\x1b[8;5HCCC");
  FEED(&t, "\x1b[5;13HD");
  FEED(&t, "\x1b[3;23HE");

  // Check final state.
  ASSERT_CELL(&t, 4, 9, 'A', "row 4, col 9 = A");
  ASSERT_CELL(&t, 4, 10, 'A', "row 4, col 10 = A");
  ASSERT_CELL(&t, 4, 11, 'A', "row 4, col 11 = A");
  ASSERT_CELL(&t, 4, 12, 'D', "row 4, col 12 = D (overlay after AAA)");

  ASSERT_CELL(&t, 2, 19, 'B', "row 2, col 19 = B");
  ASSERT_CELL(&t, 2, 22, 'E', "row 2, col 22 = E (overlay after BBB)");

  ASSERT_CELL(&t, 7, 4, 'C', "row 7, col 4 = C");

  term_destroy(&t);
  return true;
}

// ---- Real-world escape sequence combinations ----

/**
 * Simulate vim-style UI: clear screen, draw a status bar at the bottom,
 * and fill the editing area with text.
 */
static bool test_editor_like_ui(void) {
  terminal_t t;
  term_init(&t, 0);

  // 1. Clear screen and home cursor.
  FEED(&t, "\x1b[2J\x1b[H");

  // 2. Write content lines using parser_feed with strlen since
  //    line is a char array, not a literal.
  for (int r = 0; r < ROWS - 2; r++) {
    char line[32];
    int len = snprintf(line, sizeof(line), "Line %d of content\r\n", r + 1);
    parser_feed(&t.parser, (const uint8_t *)line, (size_t)len);
  }

  // 3. Move to second-to-last row and draw a status bar.
  {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "\x1b[%d;1H\x1b[7m-- STATUS BAR --\x1b[0m",
                       ROWS - 1);
    parser_feed(&t.parser, (const uint8_t *)buf, (size_t)len);
  }

  // 4. Move to last row for a command line.
  {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "\x1b[%d;1H:", ROWS);
    parser_feed(&t.parser, (const uint8_t *)buf, (size_t)len);
  }

  // Verify: last row starts with ":"
  ASSERT_CELL(&t, ROWS - 1, 0, ':', "last row has vim-like command prompt");

  // Verify: second-to-last row has status bar text
  ASSERT(assert_row(&t, ROWS - 2, "-- STATUS BAR --"),
         "status bar row correct");

  // Verify: content rows exist (spot check).
  // Line format is "Line %d of content\r\n".  Row 5, col 5 = char 6 of
  // "Line 6 of content" which is '6'.
  ASSERT_CELL(&t, 0, 0, 'L', "row 0 has content");
  ASSERT_CELL(&t, 5, 5, '6', "row 5 col 5 = '6' from 'Line 6 of content'");

  term_destroy(&t);
  return true;
}

/**
 * Simulate a long scrolling output (like `dmesg` or `make`).
 */
static bool test_long_scrolling_output(void) {
  terminal_t t;
  term_init(&t, 20);  // scrollback capacity 20

  // Write 100 lines of output, each with a unique pattern.
  // Use \r\n so each line starts at column 0, and parser_feed with
  // the actual string length (not sizeof which would include padding).
  for (int i = 0; i < 100; i++) {
    char line[COLS + 2];
    int len = snprintf(line, sizeof(line), "[%04d] output line number %d\r\n",
                       i, i);
    parser_feed(&t.parser, (const uint8_t *)line, (size_t)len);
  }

  // After 100 lines and 65 scrolls, the last scroll cleared row ROWS-1.
  // The newest visible content is on row ROWS-2 (line 99's content).
  char expected[64];
  snprintf(expected, sizeof(expected), "[%04d] output line number %d", 99, 99);
  ASSERT(assert_row(&t, ROWS - 2, expected),
         "second-to-last row shows last output line");

  // Scrollback should have 20 entries (capped at capacity).
  ASSERT_INT_EQ(scrollback_count(t.scrollback), 20,
                "scrollback capped at 20");

  // The most recent scrollback entry should be the line that just scrolled
  // off.  With 100 lines written and ROWS=36, the first scroll occurs when
  // line 35's \n pushes cy to 36 (pushing row 0 = line 0).  Lines 35..99
  // each trigger a scroll, for a total of 65 scrolls.  The most recent
  // scrollback entry (pushed by line 99's \n) is line 99-35 = 64.
  int most_recent_scrolled = 99 - (ROWS - 1);
  char sb_expected[64];
  snprintf(sb_expected, sizeof(sb_expected),
           "[%04d] output line number %d", most_recent_scrolled,
           most_recent_scrolled);
  char sb_line[COLS];
  ASSERT(scrollback_get(t.scrollback, 0, sb_line, COLS),
         "got most recent scrollback entry");
  ASSERT_INT_EQ(memcmp(sb_line, sb_expected, strlen(sb_expected)), 0,
                "most recent scrollback matches");

  // Total pushed = scrolls at lines 35 through 99 = 99-35+1 = 65.
  ASSERT_INT_EQ(scrollback_total_pushed(t.scrollback), 100 - ROWS + 1,
                "total pushed = 65 (100-36+1)");

  term_destroy(&t);
  return true;
}

// ---- Erase + rewrite patterns ----

static bool test_progress_bar_update(void) {
  terminal_t t;
  term_init(&t, 0);

  // Simulate a progress bar that updates in place.
  // Initial state: "[          ]   0%"
  FEED(&t, "[          ]   0%");

  // Go to col 1 and overwrite with progress.
  // CUP 1;2 (1-indexed) = (0,1) 0-indexed.
  FEED(&t, "\x1b[1;2H");
  // Write 5 hashes starting at col 1: cols 1-5 become "#####"
  FEED(&t, "#####");
  // Write "  50%" at col 6: cols 6-10 become "  50%"
  // Final: "[#####  50%]   0%"
  FEED(&t, "  50%");

  ASSERT(assert_row(&t, 0, "[#####  50%]   0%"),
         "progress bar first update");

  // Update to 100%.
  FEED(&t, "\x1b[1;2H");
  // Write 10 hashes: cols 1-10 become "##########"
  FEED(&t, "##########");
  // Write " 100%" at col 11: cols 11-15 become " 100%"
  // Final: "[########## 100%%" (the trailing "%" from "0%" remains at col 17)
  // Wait: " 100%" is 5 chars -> cols 11-15. Col 16 still has '0', col 17 '%'.
  FEED(&t, " 100%");

  // After second update: col 0='[', 1-10='##########', 11-15=' 100%', 16='0', 17='%'
  ASSERT_CELL(&t, 0, 0, '[', "open bracket");
  ASSERT_CELL(&t, 0, 1, '#', "first hash");
  ASSERT_CELL(&t, 0, 10, '#', "tenth hash");
  ASSERT_CELL(&t, 0, 11, ' ', "space before 100%");
  ASSERT_CELL(&t, 0, 12, '1', "100% starts");
  ASSERT_CELL(&t, 0, 15, '%', "percent sign");

  term_destroy(&t);
  return true;
}

// ---- Escape sequence interleaving ----

static bool test_osc_interleaved_with_text(void) {
  terminal_t t;
  term_init(&t, 0);

  // OSC commands should not produce visible output, but text around them
  // should still render correctly.
  FEED(&t, "Hello");
  FEED(&t, "\x1b]0;My Title\x07");  // OSC 0 – set window title
  FEED(&t, " World");

  ASSERT(assert_row(&t, 0, "Hello World"),
         "OSC does not affect text rendering");

  term_destroy(&t);
  return true;
}

static bool test_multiple_osc_in_sequence(void) {
  terminal_t t;
  term_init(&t, 0);

  // Multiple OSC commands back to back.
  FEED(&t, "\x1b]0;Title\x07\x1b]1;Icon\x07\x1b]2;Path\x07");
  FEED(&t, "After OSC");

  ASSERT(assert_row(&t, 0, "After OSC"),
         "text after multiple OSC sequences renders correctly");

  term_destroy(&t);
  return true;
}

// ---- Scrollback + screen interaction ----

static bool test_scrollback_with_ed_erase(void) {
  terminal_t t;
  term_init(&t, 10);

  // Scroll content to build up scrollback.  Use \r\n for proper line
  // endings and parser_feed with actual strlen.
  for (int i = 0; i < ROWS + 5; i++) {
    char line[COLS + 2];
    int len = snprintf(line, sizeof(line), "Line %d\r\n", i);
    parser_feed(&t.parser, (const uint8_t *)line, (size_t)len);
  }

  // With ROWS=36 and 41 lines total, scrolling started at line 35.
  // Scrolls happen at lines 35..40 = 6 scrolls -> 6 lines in scrollback.
  ASSERT_INT_EQ(scrollback_count(t.scrollback), 6,
                "6 lines in scrollback before ED 3");

  // Write some visible content, then clear screen + scrollback.
  parser_feed(&t.parser, (const uint8_t *)"Visible content", 15);
  FEED(&t, "\x1b[3J");  // ED 3 — clear screen + scrollback

  ASSERT_INT_EQ(scrollback_count(t.scrollback), 0,
                "scrollback cleared after ED 3");
  ASSERT_CELL(&t, ROWS - 1, 0, ' ', "screen content cleared");

  term_destroy(&t);
  return true;
}

static bool test_scrollback_with_ris(void) {
  terminal_t t;
  term_init(&t, 10);

  // Build scrollback using \r\n and proper length.
  for (int i = 0; i < ROWS + 3; i++) {
    char line[COLS + 2];
    int len = snprintf(line, sizeof(line), "Line %d\r\n", i);
    parser_feed(&t.parser, (const uint8_t *)line, (size_t)len);
  }

  ASSERT(scrollback_count(t.scrollback) > 0,
         "scrollback has data before RIS");

  FEED(&t, "\x1b" "c");  // RIS

  ASSERT_INT_EQ(scrollback_count(t.scrollback), 0,
                "scrollback empty after RIS");
  ASSERT_INT_EQ(scrollback_total_pushed(t.scrollback), 0,
                "scrollback total reset after RIS");

  // Screen should be clear.
  for (int r = 0; r < ROWS; r++)
    ASSERT_CELL(&t, r, 0, ' ', "screen clear after RIS");

  term_destroy(&t);
  return true;
}

// ---- Backspace and editing ----

static bool test_backspace_erase_and_rewrite(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "abc\b\bd");

  // Backspace twice from col 3 -> col 1, then write 'd'.
  // So: col 0='a', col 1='d', col 2='c' (unchanged by BS, just cursor moved)
  ASSERT_CELL(&t, 0, 0, 'a', "col 0 = a");
  ASSERT_CELL(&t, 0, 1, 'd', "col 1 = d (overwrote b)");
  ASSERT_CELL(&t, 0, 2, 'c', "col 2 = c (unchanged)");

  ASSERT_INT_EQ(t.cx, 2, "cursor at col 2");

  term_destroy(&t);
  return true;
}

// ---- Tab handling ----

static bool test_multiple_tabs(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "A\t\t\tB");

  // Tab stops at cols 8, 16, 24.
  ASSERT_CELL(&t, 0, 0, 'A', "A at col 0");
  ASSERT_CELL(&t, 0, 8, ' ', "col 8 skipped");
  ASSERT_CELL(&t, 0, 16, ' ', "col 16 skipped");
  ASSERT_CELL(&t, 0, 24, 'B', "B at col 24");

  term_destroy(&t);
  return true;
}

static bool test_tab_at_line_end(void) {
  terminal_t t;
  term_init(&t, 0);

  // Fill to near the end, then tab.
  for (int i = 0; i < 97; i++)
    FEED(&t, "X");

  ASSERT_INT_EQ(t.cx, 97, "cx = 97");

  FEED(&t, "\t");

  // Tab from col 97: the do-while loop increments past COLS, then
  // clamps to COLS-1 = 99.
  ASSERT_INT_EQ(t.cx, COLS - 1, "tab clamped to COLS-1");

  term_destroy(&t);
  return true;
}

// ---- Null / callback safety ----

static bool test_null_callbacks_do_not_crash(void) {
  terminal_t t;
  term_init(&t, 5);

  // All callbacks are set; this should just not crash.
  FEED(&t, "\x1b[1;32mHello\x1b[0m World\x1b]0;title\x07");

  // Verify some state is coherent.
  ASSERT(assert_row(&t, 0, "Hello World"), "text rendered correctly");

  term_destroy(&t);
  return true;
}

// ---- NEL (ESC E) scroll ----

static bool test_nel_scroll(void) {
  terminal_t t;
  term_init(&t, 5);

  // Fill screen with content (using \r\n for proper newlines and strlen
  // for correct length).
  for (int r = 0; r < ROWS - 1; r++) {
    char line[COLS + 2];
    int len = snprintf(line, sizeof(line), "Row %d\r\n", r);
    parser_feed(&t.parser, (const uint8_t *)line, (size_t)len);
  }

  int sb_before = scrollback_count(t.scrollback);

  // Now send NEL repeatedly to scroll.
  for (int i = 0; i < 5; i++)
    FEED(&t, "\x1b" "E");  // NEL

  // Scrollback should have grown.
  ASSERT(scrollback_count(t.scrollback) > sb_before,
         "NEL causes scrollback growth");

  term_destroy(&t);
  return true;
}

// ---- RI (ESC M) at top of screen scroll ----

static bool test_ri_scroll_at_top(void) {
  terminal_t t;
  term_init(&t, 5);

  // Write something near the top and use RI to scroll content down.
  FEED(&t, "\x1b[2;5H");  // row 1 (0-indexed), col 4
  FEED(&t, "MARK");
  FEED(&t, "\x1b[1;1H");  // home
  FEED(&t, "\x1b" "M");   // RI

  // RI at top should scroll all content down by 1 row.
  // "MARK" should now be at row 2, col 4.
  ASSERT_CELL(&t, 2, 4, 'M', "MARK moved down by RI scroll");

  // Row 0 should be blank (new empty line at top).
  ASSERT_CELL(&t, 0, 0, ' ', "blank line at top after RI");

  term_destroy(&t);
  return true;
}

// ---- Stress: rapid mixed sequences ----

static bool test_stress_mixed_sequences(void) {
  terminal_t t;
  term_init(&t, 10);

  // A mix of everything happening rapidly.
  for (int i = 0; i < 50; i++) {
    char buf[128];
    int len = snprintf(buf, sizeof(buf),
             "\x1b[%d;%dHPart%03d"
             "\x1b[%d;%dH\x1b[K"
             "\x1b[%d;%dH\x1b[1mExtra\x1b[0m",
             (i % ROWS) + 1, (i % 60) + 1, i,
             ((i + 3) % ROWS) + 1, ((i + 10) % 60) + 1,
             ((i + 7) % ROWS) + 1, ((i + 20) % 60) + 1);
    parser_feed(&t.parser, (const uint8_t *)buf, (size_t)len);
  }

  // Also cause some scrolling.
  for (int i = 0; i < ROWS + 5; i++) {
    char line[COLS + 2];
    int len = snprintf(line, sizeof(line), "Stress line %d\r\n", i);
    parser_feed(&t.parser, (const uint8_t *)line, (size_t)len);
  }

  // Just verify the terminal is in a coherent state and nothing crashed.
  // Check that all screen bytes are valid printable ASCII or spaces.
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      char ch = t.screen[r][c];
      ASSERT((ch >= 0x20 && ch <= 0x7E) || ch == ' ',
             "screen contains only printable chars or spaces");
    }
  }

  // Scrollback should be coherent.
  ASSERT(scrollback_count(t.scrollback) >= 0, "scrollback count valid");

  term_destroy(&t);
  return true;
}

// ---- CR + LF vs standalone ----

static bool test_crlf_vs_separate(void) {
  terminal_t t;
  term_init(&t, 0);

  // CR + LF together should move to start of next line.
  FEED(&t, "AAAA\r\nBBBB");

  ASSERT_CELL(&t, 0, 0, 'A', "A at row 0 col 0");
  ASSERT_CELL(&t, 1, 0, 'B', "B at row 1 col 0");

  // LF alone moves down but preserves column position.
  // After BBBB, cx=4.  \n: cy=2, cx=4.  CCCC prints at (2,4)..(2,7).
  FEED(&t, "\nCCCC");

  ASSERT_CELL(&t, 2, 0, ' ', "row 2 col 0 is space (LF preserves col)");
  ASSERT_CELL(&t, 2, 4, 'C', "C at row 2 col 4");

  // CR by itself moves cx to 0 on the same row, then print overwrites.
  // After CCCC, cx=8.  \r: cx=0.  DD: prints at (2,0) and (2,1).
  // Cols 2-3 are still spaces (never written), col 4 has 'C' (from CCCC).
  FEED(&t, "\rDD");

  ASSERT_CELL(&t, 2, 0, 'D', "D overwrote space at row 2 col 0");
  ASSERT_CELL(&t, 2, 1, 'D', "D at row 2 col 1");
  ASSERT_CELL(&t, 2, 2, ' ', "row 2 col 2 is space (unchanged)");
  ASSERT_CELL(&t, 2, 4, 'C', "C preserved at row 2 col 4");

  term_destroy(&t);
  return true;
}

// ---- Very long line (exceeding row width) ----

static bool test_very_long_line(void) {
  terminal_t t;
  term_init(&t, 0);

  // Write a line that spans 3 full rows.
  char longline[COLS * 3 + 1];
  for (int i = 0; i < COLS * 3; i++)
    longline[i] = 'A' + (i % 26);
  longline[COLS * 3] = '\0';
  FEED(&t, longline);

  // First row should be full of the first COLS chars.
  ASSERT_CELL(&t, 0, 0, 'A', "row 0 col 0 = A");
  ASSERT_CELL(&t, 0, COLS - 1, (char)('A' + ((COLS - 1) % 26)),
              "row 0 last col correct");

  // Second row should have the next set.
  ASSERT_CELL(&t, 1, 0, (char)('A' + (COLS % 26)),
              "row 1 col 0 = continuation");
  ASSERT_CELL(&t, 1, COLS - 1, (char)('A' + ((COLS * 2 - 1) % 26)),
              "row 1 last col correct");

  // Third row should have the remainder.
  ASSERT_CELL(&t, 2, 0, (char)('A' + ((COLS * 2) % 26)),
              "row 2 col 0 = continuation");

  // Cursor should be at row 3, col 0 after wrapping from row 2.
  ASSERT_INT_EQ(t.cx, 0, "cursor wrapped to col 0");
  ASSERT_INT_EQ(t.cy, 3, "cursor on row 3 after 3 rows of text");

  term_destroy(&t);
  return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  printf("integration test suite\n");
  printf("=====================\n\n");

  // Shell prompt scenarios
  TEST(shell_prompt_full_sequence);
  TEST(shell_prompt_then_user_input);
  TEST(command_output_multiple_lines);
  TEST(output_without_clear);

  // Complex cursor movement
  TEST(cursor_movement_overlay);
  TEST(rapid_cursor_and_print);

  // Real-world escape sequence combinations
  TEST(editor_like_ui);
  TEST(long_scrolling_output);

  // Erase + rewrite patterns
  TEST(progress_bar_update);

  // Escape sequence interleaving
  TEST(osc_interleaved_with_text);
  TEST(multiple_osc_in_sequence);

  // Scrollback + screen interaction
  TEST(scrollback_with_ed_erase);
  TEST(scrollback_with_ris);

  // Backspace and editing
  TEST(backspace_erase_and_rewrite);

  // Tab handling
  TEST(multiple_tabs);
  TEST(tab_at_line_end);

  // Null / callback safety
  TEST(null_callbacks_do_not_crash);

  // NEL / RI scroll
  TEST(nel_scroll);
  TEST(ri_scroll_at_top);

  // Stress tests
  TEST(stress_mixed_sequences);

  // Line handling
  TEST(crlf_vs_separate);
  TEST(very_long_line);

  printf("\n");
  printf("=================\n");
  printf("results: %d / %d passed", tests_passed, tests_run);
  if (tests_failed > 0)
    printf(", %d FAILED", tests_failed);
  printf("\n");

  return tests_failed > 0 ? 1 : 0;
}
