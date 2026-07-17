// ---------------------------------------------------------------------------
// Full document test suite for the dicTerm terminal screen buffer.
//
// Tests the terminal screen as a "document" — character placement, cursor
// movement, scrolling, erasing, scrollback interaction, and all CSI / ESC
// sequences that manipulate the screen state.  This is a unit-level test of
// the terminal state logic (what main.c does with parser callbacks) but
// exercised directly by feeding the parser and checking screen contents.
//
// Build:
//   cc -std=c2y -Wall -Wextra -Werror -Iinclude
//       src/parser.c src/scrollback.c src/test_terminal.c -o test_terminal
// Run:
//   ./test_terminal
// ---------------------------------------------------------------------------

#include "parser.h"
#include "scrollback.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Terminal dimensions (must match main.c's ROWS/COLS values for realism)
// ---------------------------------------------------------------------------

#define ROWS 36
#define COLS 100

// ---------------------------------------------------------------------------
// Test framework (same style as test_parser.c / test_scrollback.c)
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
// Terminal state (mirrors the logic in main.c's terminal_t + callbacks)
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
// Parser callbacks (same logic as main.c)
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

/** Initialise a fresh terminal with optional scrollback. */
static void term_init(terminal_t *t, int sb_cap) {
  memset(t, 0, sizeof(*t));
  for (int r = 0; r < ROWS; r++)
    memset(t->screen[r], ' ', COLS);
  t->scrollback = (sb_cap > 0) ? scrollback_create(sb_cap, COLS) : NULL;
  parser_init(&t->parser, &s_callbacks, t);
}

/** Destroy scrollback if present. */
static void term_destroy(terminal_t *t) {
  scrollback_destroy(t->scrollback);
  t->scrollback = NULL;
}

/** Feed a string literal to the parser (excluding null terminator). */
#define FEED(t, str) \
  parser_feed(&(t)->parser, (const uint8_t *)(str), sizeof(str) - 1)

/** Check that a specific cell on screen contains the expected character. */
#define ASSERT_CELL(t, row, col, ch, msg) \
  ASSERT_CHAR_EQ((t)->screen[(row)][(col)], (ch), msg)

/** Check that an entire row matches the expected string (padded with spaces). */
static bool assert_row(terminal_t *t, int row, const char *expected) {
  size_t len = strlen(expected);
  for (size_t i = 0; i < len && i < (size_t)COLS; i++) {
    if (t->screen[row][i] != expected[i]) {
      printf("\n    ASSERT_ROW: row %d col %zu: expected '%c' got '%c'\n",
             row, i, expected[i], t->screen[row][i]);
      return false;
    }
  }
  // Remainder of the row should be spaces.
  for (size_t i = len; i < (size_t)COLS; i++) {
    if (t->screen[row][i] != ' ') {
      printf("\n    ASSERT_ROW: row %d col %zu: expected ' ' got '%c'\n",
             row, i, t->screen[row][i]);
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Test cases — Full document tests
// ---------------------------------------------------------------------------

// ---- Basic printing ----

static bool test_print_single_char(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "X");

  ASSERT_CELL(&t, 0, 0, 'X', "X at (0,0)");
  ASSERT_INT_EQ(t.cx, 1, "cursor at col 1");
  ASSERT_INT_EQ(t.cy, 0, "cursor at row 0");

  term_destroy(&t);
  return true;
}

static bool test_print_multiple_chars(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "ABC");

  ASSERT_CELL(&t, 0, 0, 'A', "A at (0,0)");
  ASSERT_CELL(&t, 0, 1, 'B', "B at (0,1)");
  ASSERT_CELL(&t, 0, 2, 'C', "C at (0,2)");
  ASSERT_INT_EQ(t.cx, 3, "cursor at col 3");

  term_destroy(&t);
  return true;
}

static bool test_print_full_width(void) {
  // Fill an entire row and verify the cursor wraps to the next line.
  terminal_t t;
  term_init(&t, 0);

  char buf[COLS + 1];
  memset(buf, 'A', COLS);
  buf[COLS] = '\0';
  FEED(&t, buf);

  // All columns should be 'A'.
  for (int i = 0; i < COLS; i++)
    ASSERT_CELL(&t, 0, i, 'A', "row 0 filled with A");

  // Cursor should have wrapped: cx=0, cy=1
  ASSERT_INT_EQ(t.cx, 0, "cursor wrapped to col 0");
  ASSERT_INT_EQ(t.cy, 1, "cursor wrapped to row 1");

  term_destroy(&t);
  return true;
}

static bool test_print_and_scroll(void) {
  // Fill ROWS+1 full rows, forcing scrolls when cy exceeds the bottom.
  terminal_t t;
  term_init(&t, 5); // small scrollback

  for (int r = 0; r < ROWS + 1; r++) {
    char rowbuf[COLS];
    memset(rowbuf, 'a' + (r % 26), COLS);
    parser_feed(&t.parser, (const uint8_t *)rowbuf, COLS);
    // No newline needed — the last char wraps.
  }

  // After feeding past ROWS, we should have scrolled at least once.
  ASSERT(scrollback_count(t.scrollback) > 0, "scrollback has entries");
  // Verify some content still exists on screen (e.g. second-to-last row
  // has a character from an earlier feed).
  ASSERT_CELL(&t, ROWS - 2, 0, 'a' + ((ROWS) % 26),
              "second-to-last row has content");

  term_destroy(&t);
  return true;
}

// ---- C0 controls ----

static bool test_newline_moves_down(void) {
  terminal_t t;
  term_init(&t, 0);

  // \n preserves column position; it only moves cy down.
  FEED(&t, "A\nB\nC");

  // A at (0,0), cx becomes 1
  // \n: cy=1, cx stays 1
  // B at (1,1), cx becomes 2
  // \n: cy=2, cx stays 2
  // C at (2,2), cx becomes 3
  ASSERT_CELL(&t, 0, 0, 'A', "A at row 0");
  ASSERT_CELL(&t, 1, 1, 'B', "B at row 1, col 1");
  ASSERT_CELL(&t, 2, 2, 'C', "C at row 2, col 2");
  ASSERT_INT_EQ(t.cx, 3, "cx = 3 (newline preserves column)");
  ASSERT_INT_EQ(t.cy, 2, "cy = 2");

  term_destroy(&t);
  return true;
}

static bool test_carriage_return(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "Hello\rX");

  ASSERT_CELL(&t, 0, 0, 'X', "CR overwrites first char");
  ASSERT_CELL(&t, 0, 1, 'e', "rest preserved");
  ASSERT_INT_EQ(t.cx, 1, "cx = 1 after X");

  term_destroy(&t);
  return true;
}

static bool test_tab_stops(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "A\tB");

  ASSERT_CELL(&t, 0, 0, 'A', "A at col 0");
  ASSERT_CELL(&t, 0, 8, 'B', "tab moves to col 8");
  ASSERT_INT_EQ(t.cx, 9, "cx = 9 after B");

  term_destroy(&t);
  return true;
}

static bool test_backspace(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "AB\b");

  ASSERT_CELL(&t, 0, 0, 'A', "A at col 0");
  ASSERT_CELL(&t, 0, 1, 'B', "B at col 1");
  ASSERT_INT_EQ(t.cx, 1, "BS moves cx back to 1");

  term_destroy(&t);
  return true;
}

static bool test_backspace_at_edge(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "\b");

  // BS at col 0 should be a no-op (cx stays 0).
  ASSERT_INT_EQ(t.cx, 0, "BS at edge is no-op");

  term_destroy(&t);
  return true;
}

// ---- CSI cursor movement ----

static bool test_cuu_cursor_up(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "\n\n\n");  // cy = 3
  FEED(&t, "\x1b[A");  // CUU default = 1
  ASSERT_INT_EQ(t.cy, 2, "CUU 1 from row 3");

  FEED(&t, "\x1b[2A"); // CUU 2
  ASSERT_INT_EQ(t.cy, 0, "CUU 2 from row 2");

  term_destroy(&t);
  return true;
}

static bool test_cud_cursor_down(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "\x1b[B");  // CUD default = 1
  ASSERT_INT_EQ(t.cy, 1, "CUD 1 from row 0");

  FEED(&t, "\x1b[2B"); // CUD 2
  ASSERT_INT_EQ(t.cy, 3, "CUD 2 from row 1");

  term_destroy(&t);
  return true;
}

static bool test_cuf_cursor_forward(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "A\x1b[C");   // CUF default = 1
  ASSERT_INT_EQ(t.cx, 2, "CUF 1 from col 1");

  FEED(&t, "\x1b[5C");   // CUF 5
  ASSERT_INT_EQ(t.cx, 7, "CUF 5 from col 2");

  term_destroy(&t);
  return true;
}

static bool test_cub_cursor_back(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "ABCDE");
  ASSERT_INT_EQ(t.cx, 5, "cx = 5 after ABCDE");

  FEED(&t, "\x1b[D");   // CUB default = 1
  ASSERT_INT_EQ(t.cx, 4, "CUB 1 from col 5");

  FEED(&t, "\x1b[3D");  // CUB 3
  ASSERT_INT_EQ(t.cx, 1, "CUB 3 from col 4");

  term_destroy(&t);
  return true;
}

static bool test_cup_cursor_position(void) {
  terminal_t t;
  term_init(&t, 0);

  // CUP to row 3, col 5 (1-indexed)
  FEED(&t, "\x1b[3;5H");

  ASSERT_INT_EQ(t.cy, 2, "CUP row = 2 (0-indexed)");
  ASSERT_INT_EQ(t.cx, 4, "CUP col = 4 (0-indexed)");

  term_destroy(&t);
  return true;
}

static bool test_cup_defaults_to_1_1(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "\n\n\nABCDE");
  FEED(&t, "\x1b[H");  // CUP with no params => home (1,1)
  ASSERT_INT_EQ(t.cy, 0, "CUP home row");
  ASSERT_INT_EQ(t.cx, 0, "CUP home col");

  term_destroy(&t);
  return true;
}

static bool test_hvp_equivalent_to_cup(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "\x1b[10;20f"); // HVP (same as CUP)
  ASSERT_INT_EQ(t.cy, 9, "HVP row = 9");
  ASSERT_INT_EQ(t.cx, 19, "HVP col = 19");

  term_destroy(&t);
  return true;
}

static bool test_clamp_cursor_bounds(void) {
  terminal_t t;
  term_init(&t, 0);

  // Try to move way out of bounds.
  FEED(&t, "\x1b[999;999H");
  ASSERT_INT_EQ(t.cy, ROWS - 1, "clamped to last row");
  ASSERT_INT_EQ(t.cx, COLS - 1, "clamped to last col");

  FEED(&t, "\x1b[A");  // CUU from last row
  ASSERT_INT_EQ(t.cy, ROWS - 2, "moved up one");

  term_destroy(&t);
  return true;
}

// ---- Erase operations ----

static bool test_el_erase_to_end_of_line(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "HelloWorld");
  FEED(&t, "\x1b[4D");  // back 4
  FEED(&t, "\x1b[K");   // EL mode 0 (erase to end of line)

  ASSERT_CELL(&t, 0, 0, 'H', "H preserved");
  ASSERT_CELL(&t, 0, 1, 'e', "e preserved");
  ASSERT_CELL(&t, 0, 2, 'l', "l preserved");
  ASSERT_CELL(&t, 0, 3, 'l', "l preserved");
  ASSERT_CELL(&t, 0, 4, 'o', "o preserved");
  ASSERT_CELL(&t, 0, 5, 'W', "W preserved");
  // Cols 6-9 should be spaces (erased from cursor at col 6)
  ASSERT_CELL(&t, 0, 6, ' ', "col 6 erased");
  ASSERT_CELL(&t, 0, 9, ' ', "col 9 erased");

  term_destroy(&t);
  return true;
}

static bool test_el_erase_to_start_of_line(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "ABCDEFGHIJ");
  FEED(&t, "\x1b[4D");   // back to col 6
  FEED(&t, "\x1b[1K");   // EL mode 1 (erase to start of line)

  // Cols 0-6 should be spaces, 7-9 preserved
  ASSERT_CELL(&t, 0, 0, ' ', "col 0 erased");
  ASSERT_CELL(&t, 0, 6, ' ', "col 6 erased (cursor pos)");
  ASSERT_CELL(&t, 0, 7, 'H', "col 7 preserved");
  ASSERT_CELL(&t, 0, 9, 'J', "col 9 preserved");

  term_destroy(&t);
  return true;
}

static bool test_el_erase_entire_line(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "HelloWorld");
  FEED(&t, "\x1b[2K");  // EL mode 2 (entire line)

  for (int i = 0; i < COLS; i++)
    ASSERT_CELL(&t, 0, i, ' ', "entire line erased");

  term_destroy(&t);
  return true;
}

static bool test_ed_erase_to_end_of_screen(void) {
  terminal_t t;
  term_init(&t, 0);

  // Fill some content across multiple rows.  Use \r\n so that each line
  // starts at column 0 (LF alone preserves the column position).
  FEED(&t, "ROW0\r\nROW1\r\nROW2\r\nROW3");
  // Cursor is now on row 3 at col 0.
  // Move cursor to row 1, col 2 and do ED 0
  FEED(&t, "\x1b[2;3H");
  FEED(&t, "\x1b[J");  // ED mode 0 (default)

  // Row 0 should be unchanged (fully before cursor)
  ASSERT_CELL(&t, 0, 0, 'R', "row0 R preserved");
  ASSERT_CELL(&t, 0, 3, '0', "row0 0 preserved");

  // Row 1 (cursor row): cols 0-1 preserved, 2-end erased
  ASSERT_CELL(&t, 1, 0, 'R', "row1 R preserved (cursor row)");
  ASSERT_CELL(&t, 1, 1, 'O', "row1 O preserved (cursor row)");
  ASSERT_CELL(&t, 1, 2, ' ', "row1 col2 erased (cursor)");
  ASSERT_CELL(&t, 1, 3, ' ', "row1 col3 erased");

  // Row 2 onward should be all spaces
  for (int r = 2; r < ROWS; r++)
    ASSERT_CELL(&t, r, 0, ' ', "rows 2+ erased");

  term_destroy(&t);
  return true;
}

static bool test_ed_erase_to_start_of_screen(void) {
  terminal_t t;
  term_init(&t, 0);

  // Use \r\n so each line starts at column 0.
  FEED(&t, "AAAA\r\nBBBB\r\nCCCC\r\nDDDD");
  FEED(&t, "\x1b[2;3H"); // cursor to row 1, col 2 (0-indexed)
  FEED(&t, "\x1b[1J");   // ED mode 1 – erase from start to cursor

  // Row 0 should be completely erased
  for (int i = 0; i < COLS; i++)
    ASSERT_CELL(&t, 0, i, ' ', "row 0 erased");

  // Row 1 (cursor row): cols 0..2 erased (cursor at col 2, inclusive),
  // col 3 onward preserved.
  ASSERT_CELL(&t, 1, 0, ' ', "row1 col0 erased");
  ASSERT_CELL(&t, 1, 1, ' ', "row1 col1 erased");
  ASSERT_CELL(&t, 1, 2, ' ', "row1 col2 erased (cursor column)");
  ASSERT_CELL(&t, 1, 3, 'B', "row1 col3 preserved (after cursor)");

  // Rows 2+ should be preserved
  ASSERT_CELL(&t, 2, 0, 'C', "row2 preserved");
  ASSERT_CELL(&t, 3, 0, 'D', "row3 preserved");

  term_destroy(&t);
  return true;
}

static bool test_ed_erase_entire_screen(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "Hello\nWorld\nTest");
  FEED(&t, "\x1b[2J");  // ED mode 2

  for (int r = 0; r < ROWS; r++)
    ASSERT_CELL(&t, r, 0, ' ', "all rows erased");

  term_destroy(&t);
  return true;
}

static bool test_ed_erase_screen_and_scrollback(void) {
  terminal_t t;
  term_init(&t, 10);

  // Fill and scroll to populate scrollback.
  for (int r = 0; r < ROWS + 5; r++) {
    char rowbuf[COLS];
    memset(rowbuf, 'a' + (r % 26), COLS);
    parser_feed(&t.parser, (const uint8_t *)rowbuf, COLS);
  }

  ASSERT(scrollback_count(t.scrollback) > 0, "scrollback has data before ED 3");

  FEED(&t, "\x1b[3J");  // ED mode 3

  // Screen should be clear.
  for (int r = 0; r < ROWS; r++)
    ASSERT_CELL(&t, r, 0, ' ', "screen clear after ED 3");

  // Scrollback should be empty.
  ASSERT_INT_EQ(scrollback_count(t.scrollback), 0,
                "scrollback cleared after ED 3");

  term_destroy(&t);
  return true;
}

// ---- Cursor save/restore ----

static bool test_csi_save_restore_cursor(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "\x1b[10;20H");  // move to (9,19)
  FEED(&t, "\x1b[s");       // save

  FEED(&t, "\x1b[3;5H");    // move to (2,4)
  ASSERT_INT_EQ(t.cx, 4, "after move cx=4");
  ASSERT_INT_EQ(t.cy, 2, "after move cy=2");

  FEED(&t, "\x1b[u");       // restore
  ASSERT_INT_EQ(t.cx, 19, "restored cx=19");
  ASSERT_INT_EQ(t.cy, 9, "restored cy=9");

  term_destroy(&t);
  return true;
}

static bool test_esc_decsc_decrc(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "\x1b[15;30H"); // move to (14,29)
  FEED(&t, "\x1b" "7");    // DECSC (ESC 7) save

  FEED(&t, "\x1b[1;1H");   // move home
  ASSERT_INT_EQ(t.cx, 0, "home cx=0");
  ASSERT_INT_EQ(t.cy, 0, "home cy=0");

  FEED(&t, "\x1b" "8");    // DECRC (ESC 8) restore
  ASSERT_INT_EQ(t.cx, 29, "restored cx=29");
  ASSERT_INT_EQ(t.cy, 14, "restored cy=14");

  term_destroy(&t);
  return true;
}

// ---- ESC sequences ----

static bool test_esc_ind_index(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "A");
  FEED(&t, "\x1b" "D");   // IND

  ASSERT_INT_EQ(t.cy, 1, "IND moves cursor down");
  ASSERT_INT_EQ(t.cx, 1, "cx preserved by IND");

  term_destroy(&t);
  return true;
}

static bool test_esc_ri_reverse_index(void) {
  terminal_t t;
  term_init(&t, 0);

  // Move to row 2, col 5
  FEED(&t, "\x1b[3;6H");
  FEED(&t, "\x1b" "M");   // RI

  ASSERT_INT_EQ(t.cy, 1, "RI moves cursor up");
  ASSERT_INT_EQ(t.cx, 5, "cx preserved by RI");

  // RI at top of screen should scroll content down.
  FEED(&t, "\x1b" "M");   // RI again -> cy = 0
  ASSERT_INT_EQ(t.cy, 0, "RI at row 0 stays at 0");

  // Feed another RI; screen should scroll down.
  FEED(&t, "\x1b[2;3H");   // move to row 1, col 2
  FEED(&t, "X");           // write X at (1,2)
  FEED(&t, "\x1b[1;1H");   // home
  FEED(&t, "\x1b" "M");   // RI at top -> scroll down

  ASSERT_CELL(&t, 0, 0, ' ', "new blank line at top after RI scroll");
  // The 'X' should have moved to row 2, col 2
  ASSERT_CELL(&t, 2, 2, 'X', "X moved down by RI scroll");

  term_destroy(&t);
  return true;
}

static bool test_esc_nel_next_line(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "ABCDE");
  FEED(&t, "\x1b" "E");   // NEL

  ASSERT_INT_EQ(t.cx, 0, "NEL resets cx to 0");
  ASSERT_INT_EQ(t.cy, 1, "NEL moves cy down");

  term_destroy(&t);
  return true;
}

static bool test_esc_ris_reset(void) {
  terminal_t t;
  term_init(&t, 5);

  FEED(&t, "Hello\nWorld\nTest");
  FEED(&t, "\x1b[10;15H");

  // Push some scrollback.
  for (int i = 0; i < ROWS + 3; i++) {
    char rowbuf[COLS];
    memset(rowbuf, 'Z', COLS);
    parser_feed(&t.parser, (const uint8_t *)rowbuf, COLS);
  }

  ASSERT(scrollback_count(t.scrollback) > 0, "scrollback has data before RIS");

  FEED(&t, "\x1b" "c");   // RIS

  // Screen should be clear.
  for (int r = 0; r < ROWS; r++)
    ASSERT_CELL(&t, r, 0, ' ', "screen clear after RIS");

  // Cursor should be at home.
  ASSERT_INT_EQ(t.cx, 0, "cx=0 after RIS");
  ASSERT_INT_EQ(t.cy, 0, "cy=0 after RIS");

  // Scrollback should be reset.
  ASSERT_INT_EQ(scrollback_count(t.scrollback), 0,
                "scrollback reset after RIS");
  ASSERT_INT_EQ(scrollback_total_pushed(t.scrollback), 0,
                "scrollback total reset after RIS");

  term_destroy(&t);
  return true;
}

// ---- Scrollback interaction ----

static bool test_scrollback_populated_on_scroll(void) {
  terminal_t t;
  term_init(&t, 100);

  // Fill the screen and scroll.  ROWS+3 feeds means ROWS+3 = 39 iterations
  // (r = 0..38).  Scrolls happen when r >= ROWS-1 (i.e. r >= 35), so we
  // get 4 scrolls.
  for (int r = 0; r < ROWS + 3; r++) {
    char rowbuf[COLS];
    memset(rowbuf, 'a' + (r % 26), COLS);
    parser_feed(&t.parser, (const uint8_t *)rowbuf, COLS);
  }

  // Scrollback should have exactly 4 lines.
  ASSERT_INT_EQ(scrollback_count(t.scrollback), 4,
                "4 lines in scrollback after 4 scrolls");

  // Verify the most recent scrollback entry (index 0) is from the 4th scroll,
  // which pushed screen[0] containing content from original row 3.
  char sb_line[COLS];
  ASSERT(scrollback_get(t.scrollback, 0, sb_line, COLS),
         "got most recent scrollback line");
  ASSERT_CHAR_EQ(sb_line[0], 'a' + (3 % 26),
                 "most recent scrollback is row index 3 content");

  term_destroy(&t);
  return true;
}

static bool test_scrollback_preserves_content(void) {
  terminal_t t;
  term_init(&t, 100);

  // Write distinct content on each row.  ROWS = 36 feeds here.
  // r=35 wraps and causes the 1st scroll (pushes original row 0 = 'A').
  for (int r = 0; r < ROWS; r++) {
    char rowbuf[COLS];
    memset(rowbuf, 'A' + (r % 26), COLS);
    parser_feed(&t.parser, (const uint8_t *)rowbuf, COLS);
  }
  // After first loop: 1 scroll (pushed 'A').

  // Scroll 5 more lines.  Each causes another scroll.
  for (int r = 0; r < 5; r++) {
    char rowbuf[COLS];
    memset(rowbuf, 'Z', COLS);
    parser_feed(&t.parser, (const uint8_t *)rowbuf, COLS);
  }
  // Total scrolls = 1 + 5 = 6.  Pushes (in order):
  //   1:'A', 2:'B', 3:'C', 4:'D', 5:'E', 6:'F'

  // Scrollback should have 6 entries.
  ASSERT_INT_EQ(scrollback_count(t.scrollback), 6, "6 scrollback entries");

  char sb_line[COLS];
  // Most recent entry (index 0) is the 6th scroll, which pushed 'F'.
  ASSERT(scrollback_get(t.scrollback, 0, sb_line, COLS),
         "got scrollback[0]");
  ASSERT_CHAR_EQ(sb_line[0], 'F',
                 "scrollback[0] is row 5 content (6th scroll)");

  // Oldest retained entry (index 5) is the 1st scroll, which pushed 'A'.
  ASSERT(scrollback_get(t.scrollback, 5, sb_line, COLS),
         "got scrollback[5]");
  ASSERT_CHAR_EQ(sb_line[0], 'A',
                 "scrollback[5] is original row 0 content");

  term_destroy(&t);
  return true;
}

static bool test_scrollback_ring_wrap(void) {
  terminal_t t;
  term_init(&t, 5);  // small scrollback (only 5 lines)

  // Feed ROWS+10 full rows.  ROWS=36, so 46 iterations (r=0..45).
  // Scrolls occur starting from r=35 (ROWS-1), giving 11 total scrolls.
  for (int r = 0; r < ROWS + 10; r++) {
    char rowbuf[COLS];
    memset(rowbuf, 'a' + (r % 26), COLS);
    parser_feed(&t.parser, (const uint8_t *)rowbuf, COLS);
  }

  // Scrollback should be capped at 5.
  ASSERT_INT_EQ(scrollback_count(t.scrollback), 5,
                "scrollback capped at capacity");

  // The most recent entry (index 0) is from the 11th scroll,
  // which pushed 'k' = 'a' + 10.
  char sb_line[COLS];
  ASSERT(scrollback_get(t.scrollback, 0, sb_line, COLS),
         "got most recent scrollback");
  ASSERT_CHAR_EQ(sb_line[0], 'a' + (10 % 26),
                 "most recent is from scroll 11 (content 'k')");

  term_destroy(&t);
  return true;
}

// ---- Mixed operations ----

static bool test_write_then_erase_then_write(void) {
  terminal_t t;
  term_init(&t, 0);

  FEED(&t, "Hello");          // cx = 5
  FEED(&t, "\x1b[2K");        // EL 2 – erase entire line, cx stays 5
  ASSERT_CELL(&t, 0, 0, ' ', "line erased");

  FEED(&t, "World");           // starts at col 5
  ASSERT_CELL(&t, 0, 5, 'W', "W after erase and rewrite (at col 5)");
  ASSERT_CELL(&t, 0, 6, 'o', "o at col 6");

  term_destroy(&t);
  return true;
}

static bool test_multi_line_edit(void) {
  terminal_t t;
  term_init(&t, 0);

  // Write text on multiple lines, go back and edit.
  // \r\n puts each line at column 0 (LF alone preserves column).
  FEED(&t, "Line 1\r\nLine 2\r\nLine 3");

  ASSERT(assert_row(&t, 0, "Line 1"), "row 0 correct");
  ASSERT(assert_row(&t, 1, "Line 2"), "row 1 correct");
  ASSERT(assert_row(&t, 2, "Line 3"), "row 2 correct");

  // Go back to row 1 and overwrite.
  FEED(&t, "\x1b[2;1H");  // row 1, col 0
  FEED(&t, "CHANGED");

  ASSERT(assert_row(&t, 0, "Line 1"), "row 0 unchanged");
  ASSERT(assert_row(&t, 1, "CHANGED"), "row 1 overwritten");

  term_destroy(&t);
  return true;
}

static bool test_line_wrap_with_long_line(void) {
  terminal_t t;
  term_init(&t, 0);

  // Write a line that's exactly COLS chars + 1 more to trigger wrap.
  char longline[COLS + 10];
  memset(longline, 'A', COLS);
  longline[COLS] = 'B';
  longline[COLS + 1] = '\0';

  FEED(&t, longline);

  // Last char of first row should be 'A'.
  ASSERT_CELL(&t, 0, COLS - 1, 'A', "last col of row 0 is A");
  // First char of second row should be 'B' (the overflow char).
  ASSERT_CELL(&t, 1, 0, 'B', "first col of row 1 is B (wrapped)");

  term_destroy(&t);
  return true;
}

static bool test_scroll_at_bottom_with_newline(void) {
  terminal_t t;
  term_init(&t, 3);

  // Fill the screen completely.
  for (int r = 0; r < ROWS; r++) {
    char rowbuf[COLS];
    memset(rowbuf, 'A' + (r % 26), COLS);
    parser_feed(&t.parser, (const uint8_t *)rowbuf, COLS);
  }
  // After ROWS full rows the cursor wrapped at the boundary, causing
  // one scroll during the last feed.  cy = ROWS-1, scrollback = 1.

  // Send a newline to trigger another scroll.
  FEED(&t, "\n");

  // After the second scroll, scrollback should have 2 entries.
  ASSERT_INT_EQ(scrollback_count(t.scrollback), 2,
                "scrollback has 2 entries after LF at bottom");

  // The last row should be blank (new line after scroll).
  ASSERT_CELL(&t, ROWS - 1, 0, ' ', "last row is blank after scroll");

  term_destroy(&t);
  return true;
}

static bool test_scroll_at_bottom_with_print(void) {
  terminal_t t;
  term_init(&t, 3);

  // Fill the screen completely.  Each row of COLS chars wraps to the
  // next row.  After ROWS rows the cursor wraps at the bottom boundary,
  // causing a scroll (one entry in scrollback).
  for (int r = 0; r < ROWS; r++) {
    char rowbuf[COLS];
    memset(rowbuf, 'X', COLS);
    parser_feed(&t.parser, (const uint8_t *)rowbuf, COLS);
  }
  // cy = ROWS-1, scrollback = 1.

  // Write one more full row to trigger a second scroll via wrap.
  char rowbuf[COLS];
  memset(rowbuf, 'Y', COLS);
  parser_feed(&t.parser, (const uint8_t *)rowbuf, COLS);

  ASSERT(scrollback_count(t.scrollback) > 0, "scrollback has data");
  term_destroy(&t);
  return true;
}

// ---- Edge cases ----

static bool test_empty_input(void) {
  terminal_t t;
  term_init(&t, 0);

  parser_feed(&t.parser, (const uint8_t *)"", 0);

  ASSERT_INT_EQ(t.cx, 0, "cx unchanged");
  ASSERT_INT_EQ(t.cy, 0, "cy unchanged");
  ASSERT_INT_EQ(t.parser.state, PARSER_GROUND, "state is GROUND");

  term_destroy(&t);
  return true;
}

static bool test_rapid_sequences(void) {
  terminal_t t;
  term_init(&t, 5);

  // Feed a rapid burst of cursor movements and prints.
  for (int i = 0; i < 100; i++) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH%c",
                       (i % ROWS) + 1, (i % COLS) + 1,
                       'A' + (i % 26));
    parser_feed(&t.parser, (const uint8_t *)buf, (size_t)len);
  }

  // The last write should be at the final cursor position.
  int last_idx = 99;
  int expected_row = last_idx % ROWS;
  int expected_col = last_idx % COLS;
  ASSERT_CHAR_EQ(t.screen[expected_row][expected_col],
                 'A' + (last_idx % 26),
                 "last rapid-write char is correct");

  term_destroy(&t);
  return true;
}

static bool test_no_scrollback_does_not_crash(void) {
  terminal_t t;
  term_init(&t, 0);  // no scrollback

  // Feed various sequences that touch scrollback.
  FEED(&t, "Hello\nWorld\nTest\nEnd");
  for (int r = 0; r < ROWS + 5; r++) {
    char rowbuf[COLS];
    memset(rowbuf, 'X', COLS);
    parser_feed(&t.parser, (const uint8_t *)rowbuf, COLS);
  }

  // Screen should still be consistent.
  ASSERT_CELL(&t, ROWS - 1, 0, 'X', "last row has X after scroll");

  term_destroy(&t);
  return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  printf("terminal full document test suite\n");
  printf("=================================\n\n");

  // Basic printing
  TEST(print_single_char);
  TEST(print_multiple_chars);
  TEST(print_full_width);
  TEST(print_and_scroll);

  // C0 controls
  TEST(newline_moves_down);
  TEST(carriage_return);
  TEST(tab_stops);
  TEST(backspace);
  TEST(backspace_at_edge);

  // CSI cursor movement
  TEST(cuu_cursor_up);
  TEST(cud_cursor_down);
  TEST(cuf_cursor_forward);
  TEST(cub_cursor_back);
  TEST(cup_cursor_position);
  TEST(cup_defaults_to_1_1);
  TEST(hvp_equivalent_to_cup);
  TEST(clamp_cursor_bounds);

  // Erase operations
  TEST(el_erase_to_end_of_line);
  TEST(el_erase_to_start_of_line);
  TEST(el_erase_entire_line);
  TEST(ed_erase_to_end_of_screen);
  TEST(ed_erase_to_start_of_screen);
  TEST(ed_erase_entire_screen);
  TEST(ed_erase_screen_and_scrollback);

  // Cursor save/restore
  TEST(csi_save_restore_cursor);
  TEST(esc_decsc_decrc);

  // ESC sequences
  TEST(esc_ind_index);
  TEST(esc_ri_reverse_index);
  TEST(esc_nel_next_line);
  TEST(esc_ris_reset);

  // Scrollback interaction
  TEST(scrollback_populated_on_scroll);
  TEST(scrollback_preserves_content);
  TEST(scrollback_ring_wrap);

  // Mixed operations
  TEST(write_then_erase_then_write);
  TEST(multi_line_edit);
  TEST(line_wrap_with_long_line);
  TEST(scroll_at_bottom_with_newline);
  TEST(scroll_at_bottom_with_print);

  // Edge cases
  TEST(empty_input);
  TEST(rapid_sequences);
  TEST(no_scrollback_does_not_crash);

  printf("\n");
  printf("=================================\n");
  printf("results: %d / %d passed", tests_passed, tests_run);
  if (tests_failed > 0)
    printf(", %d FAILED", tests_failed);
  printf("\n");

  return tests_failed > 0 ? 1 : 0;
}
