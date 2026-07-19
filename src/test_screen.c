/**
 * @file test_screen.c
 * @brief Unit tests for the screen buffer (screen.h / screen.c).
 *
 * Verifies allocation, deallocation, cell access, SGR attribute
 * application, row clearing, and erase operations, including NULL
 * safety and edge cases.
 */

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "screen.h"

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
  printf("  %s ... ", name); \
  tests_run++;          \
} while(0)

#define PASS() do { \
  printf("OK\n");   \
  tests_passed++;   \
} while(0)

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/// @brief Verify that screen_buf_new allocates a properly initialised buffer
///        and screen_buf_free releases it without leaking.
static void test_new_free(void) {
  screen_buf_t *sb = screen_buf_new(10, 20);
  assert(sb != NULL);
  assert(sb->rows == 10);
  assert(sb->cols == 20);
  assert(sb->cells != NULL);

  // All cells should be space with default fg
  for (int r = 0; r < 10; r++) {
    for (int c = 0; c < 20; c++) {
      screen_cell_t *cell = &sb->cells[r * 20 + c];
      assert(cell->ch == ' ');
      assert(cell->fg[0] == 220);
      assert(cell->fg[1] == 220);
      assert(cell->fg[2] == 220);
      assert(cell->bg[0] == 0);
      assert(cell->bg[1] == 0);
      assert(cell->bg[2] == 0);
      assert(cell->bold == 0);
      assert(cell->italic == 0);
      assert(cell->underline == 0);
      assert(cell->blink == 0);
    }
  }

  screen_buf_free(sb);
}

/// @brief Test screen_buf_new with invalid arguments returns NULL.
static void test_new_invalid_args(void) {
  assert(screen_buf_new(0, 10) == NULL);
  assert(screen_buf_new(10, 0) == NULL);
  assert(screen_buf_new(-1, 10) == NULL);
  assert(screen_buf_new(10, -5) == NULL);
}

/// @brief Test screen_buf_free with NULL is safe.
static void test_free_null(void) {
  screen_buf_free(NULL);
  // Should not crash
}

/// @brief Test screen_buf_put and screen_buf_cell with valid and
///        out-of-bounds indices.
static void test_put_and_cell(void) {
  screen_buf_t *sb = screen_buf_new(5, 10);

  screen_buf_put(sb, 2, 3, 'X');
  screen_cell_t *c = screen_buf_cell(sb, 2, 3);
  assert(c != NULL);
  assert(c->ch == 'X');

  // Out of bounds should be no-op / return NULL
  screen_buf_put(sb, 100, 0, '?');     // no crash
  screen_buf_put(sb, 0, 100, '?');     // no crash
  screen_buf_put(sb, -1, 0, '?');      // no crash
  assert(screen_buf_cell(sb, -1, 0) == NULL);
  assert(screen_buf_cell(sb, 0, -1) == NULL);
  assert(screen_buf_cell(sb, 100, 0) == NULL);
  assert(screen_buf_cell(sb, 0, 100) == NULL);

  screen_buf_free(sb);
}

/// @brief Test NULL safety for put and cell.
static void test_put_cell_null(void) {
  screen_buf_put(NULL, 0, 0, 'X');     // no crash
  assert(screen_buf_cell(NULL, 0, 0) == NULL);
}

/// @brief Verify screen_buf_apply_sgr sets foreground and background colours.
static void test_apply_sgr(void) {
  screen_buf_t *sb = screen_buf_new(3, 5);

  uint8_t fg[3] = {255, 0, 0};
  uint8_t bg[3] = {0, 0, 255};
  screen_buf_apply_sgr(sb, 1, 1, fg, bg);

  screen_cell_t *c = screen_buf_cell(sb, 1, 1);
  assert(c != NULL);
  assert(c->fg[0] == 255 && c->fg[1] == 0 && c->fg[2] == 0);
  assert(c->bg[0] == 0 && c->bg[1] == 0 && c->bg[2] == 255);

  // Style flags should not be touched by apply_sgr
  assert(c->bold == 0);
  assert(c->italic == 0);
  assert(c->underline == 0);
  assert(c->blink == 0);

  screen_buf_free(sb);
}

/// @brief Test apply_sgr with NULL buffer.
static void test_apply_sgr_null(void) {
  uint8_t fg[3] = {255, 0, 0};
  uint8_t bg[3] = {0, 0, 255};
  screen_buf_apply_sgr(NULL, 0, 0, fg, bg);   // no crash
}

/// @brief Test apply_sgr out of bounds.
static void test_apply_sgr_oob(void) {
  screen_buf_t *sb = screen_buf_new(3, 5);
  uint8_t fg[3] = {255, 0, 0};
  uint8_t bg[3] = {0, 0, 255};
  screen_buf_apply_sgr(sb, 100, 0, fg, bg);   // no crash
  screen_buf_free(sb);
}

/// @brief Test screen_buf_clear_row with and without colour reset.
static void test_clear_row(void) {
  screen_buf_t *sb = screen_buf_new(4, 8);
  uint8_t fg[3] = {200, 100, 50};
  uint8_t bg[3] = {10, 20, 30};

  screen_buf_apply_sgr(sb, 2, 4, fg, bg);
  screen_buf_put(sb, 2, 4, 'A');

  // Clear without resetting colours
  screen_buf_clear_row(sb, 2, false);
  screen_cell_t *c = screen_buf_cell(sb, 2, 4);
  assert(c->ch == ' ');                     // character reset
  assert(c->fg[0] == 200);                  // colour preserved
  assert(c->bg[0] == 10);                   // colour preserved
  assert(c->bold == 0);                     // style flags preserved
  assert(c->underline == 0);

  // Clear with resetting colours
  screen_buf_apply_sgr(sb, 1, 1, fg, bg);
  screen_buf_clear_row(sb, 1, true);
  c = screen_buf_cell(sb, 1, 1);
  assert(c->ch == ' ');
  assert(c->fg[0] == 220);                  // reset to default
  assert(c->bg[0] == 0);                    // reset to transparent
  assert(c->bold == 0);                     // style flags reset
  assert(c->underline == 0);
  assert(c->italic == 0);
  assert(c->blink == 0);

  screen_buf_free(sb);
}

/// @brief Test clear_row with NULL and out-of-bounds.
static void test_clear_row_edge(void) {
  screen_buf_t *sb = screen_buf_new(3, 5);
  screen_buf_clear_row(NULL, 0, false);     // no crash
  screen_buf_clear_row(sb, -1, false);      // no crash
  screen_buf_clear_row(sb, 100, false);     // no crash
  screen_buf_free(sb);
}

/// @brief Test erase display modes (0 = cursor→end, 1 = start→cursor, 2 = all).
static void test_erase_display(void) {
  screen_buf_t *sb = screen_buf_new(3, 5);

  // Fill entire screen with 'X'
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 5; c++)
      screen_buf_put(sb, r, c, 'X');

  // Erase from (1, 2) to end (mode 0)
  screen_buf_erase_display(sb, 1, 2, 0, true);

  // Cells before should remain 'X'
  assert(screen_buf_cell(sb, 0, 0)->ch == 'X');
  assert(screen_buf_cell(sb, 1, 1)->ch == 'X');
  // Cells at/after cursor should be spaces
  assert(screen_buf_cell(sb, 1, 2)->ch == ' ');
  assert(screen_buf_cell(sb, 2, 4)->ch == ' ');

  screen_buf_free(sb);
}

/// @brief Test erase display mode 1 (start to cursor).
static void test_erase_display_mode1(void) {
  screen_buf_t *sb = screen_buf_new(3, 5);

  // Fill entire screen with 'X'
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 5; c++)
      screen_buf_put(sb, r, c, 'X');

  // Erase from start to (1, 2) (mode 1)
  screen_buf_erase_display(sb, 1, 2, 1, false);

  // Cells from start to cursor should be spaces
  assert(screen_buf_cell(sb, 0, 0)->ch == ' ');
  assert(screen_buf_cell(sb, 0, 4)->ch == ' ');
  assert(screen_buf_cell(sb, 1, 0)->ch == ' ');
  assert(screen_buf_cell(sb, 1, 1)->ch == ' ');
  assert(screen_buf_cell(sb, 1, 2)->ch == ' ');
  // Cells after cursor on same row should remain
  assert(screen_buf_cell(sb, 1, 3)->ch == 'X');
  assert(screen_buf_cell(sb, 1, 4)->ch == 'X');
  // Rows after cursor row should be untouched
  assert(screen_buf_cell(sb, 2, 0)->ch == 'X');
  assert(screen_buf_cell(sb, 2, 4)->ch == 'X');

  screen_buf_free(sb);
}

/// @brief Test erase display mode 2 (entire screen) with and without colour reset.
static void test_erase_display_mode2(void) {
  screen_buf_t *sb = screen_buf_new(2, 3);

  // Apply SGR to a cell
  uint8_t fg[3] = {100, 0, 0};
  uint8_t bg[3] = {0, 100, 0};
  screen_buf_apply_sgr(sb, 1, 1, fg, bg);
  screen_buf_put(sb, 1, 1, 'Z');

  // Erase entire screen WITHOUT colour reset
  screen_buf_erase_display(sb, 0, 0, 2, false);

  assert(screen_buf_cell(sb, 1, 1)->ch == ' ');
  // Colours survive
  assert(screen_buf_cell(sb, 1, 1)->fg[0] == 100);
  assert(screen_buf_cell(sb, 1, 1)->bg[1] == 100);

  screen_buf_free(sb);
}

/// @brief Test erase display mode 2 WITH colour reset.
static void test_erase_display_mode2_reset(void) {
  screen_buf_t *sb = screen_buf_new(2, 3);

  uint8_t fg[3] = {100, 0, 0};
  uint8_t bg[3] = {0, 100, 0};
  screen_buf_apply_sgr(sb, 0, 0, fg, bg);
  screen_buf_put(sb, 0, 0, 'Z');

  // Erase entire screen WITH colour reset
  screen_buf_erase_display(sb, 0, 0, 2, true);

  assert(screen_buf_cell(sb, 0, 0)->ch == ' ');
  assert(screen_buf_cell(sb, 0, 0)->fg[0] == 220);
  assert(screen_buf_cell(sb, 0, 0)->bg[1] == 0);

  screen_buf_free(sb);
}

/// @brief Test erase_display with NULL.
static void test_erase_display_null(void) {
  screen_buf_erase_display(NULL, 0, 0, 0, false);   // no crash
}

/// @brief Test erase line modes (0 = cursor→end, 1 = start→cursor, 2 = all).
static void test_erase_line(void) {
  screen_buf_t *sb = screen_buf_new(3, 5);

  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 5; c++)
      screen_buf_put(sb, r, c, 'X');

  // Erase from (1, 2) to end of line (mode 0)
  screen_buf_erase_line(sb, 1, 2, 0, true);
  assert(screen_buf_cell(sb, 1, 0)->ch == 'X');
  assert(screen_buf_cell(sb, 1, 1)->ch == 'X');
  assert(screen_buf_cell(sb, 1, 2)->ch == ' ');
  assert(screen_buf_cell(sb, 1, 4)->ch == ' ');

  // Erase entire line (mode 2)
  screen_buf_erase_line(sb, 0, 0, 2, true);
  for (int c = 0; c < 5; c++)
    assert(screen_buf_cell(sb, 0, c)->ch == ' ');

  screen_buf_free(sb);
}

/// @brief Test erase line mode 1 (start to cursor).
static void test_erase_line_mode1(void) {
  screen_buf_t *sb = screen_buf_new(3, 5);

  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 5; c++)
      screen_buf_put(sb, r, c, 'X');

  // Erase from start to (0, 2) (mode 1)
  screen_buf_erase_line(sb, 0, 2, 1, false);
  assert(screen_buf_cell(sb, 0, 0)->ch == ' ');
  assert(screen_buf_cell(sb, 0, 1)->ch == ' ');
  assert(screen_buf_cell(sb, 0, 2)->ch == ' ');
  assert(screen_buf_cell(sb, 0, 3)->ch == 'X');   // preserved
  assert(screen_buf_cell(sb, 0, 4)->ch == 'X');   // preserved
  // Other rows untouched
  assert(screen_buf_cell(sb, 1, 0)->ch == 'X');

  screen_buf_free(sb);
}

/// @brief Test erase_line with NULL or OOB.
static void test_erase_line_edge(void) {
  screen_buf_t *sb = screen_buf_new(3, 5);

  screen_buf_erase_line(NULL, 0, 0, 0, false);   // no crash
  screen_buf_erase_line(sb, -1, 0, 0, false);    // no crash
  screen_buf_erase_line(sb, 100, 0, 0, false);   // no crash

  screen_buf_free(sb);
}

/// @brief Test that put() does not modify colour/style attributes.
static void test_put_preserves_attrs(void) {
  screen_buf_t *sb = screen_buf_new(2, 2);

  uint8_t fg[3] = {10, 20, 30};
  uint8_t bg[3] = {40, 50, 60};
  screen_buf_apply_sgr(sb, 0, 0, fg, bg);

  // Write a character to the same cell
  screen_buf_put(sb, 0, 0, 'Z');

  screen_cell_t *c = screen_buf_cell(sb, 0, 0);
  assert(c->ch == 'Z');
  assert(c->fg[0] == 10 && c->fg[1] == 20 && c->fg[2] == 30);
  assert(c->bg[0] == 40 && c->bg[1] == 50 && c->bg[2] == 60);

  screen_buf_free(sb);
}

/// @brief Coherence test: apply SGR to multiple cells, modify, erase.
static void test_multi_cell_operations(void) {
  screen_buf_t *sb = screen_buf_new(4, 6);

  uint8_t red[3]   = {255, 0, 0};
  uint8_t green[3] = {0, 255, 0};
  uint8_t blue[3]  = {0, 0, 255};
  uint8_t black[3] = {0, 0, 0};

  // Set different colours on different cells
  screen_buf_apply_sgr(sb, 0, 0, red, black);
  screen_buf_apply_sgr(sb, 1, 1, green, black);
  screen_buf_apply_sgr(sb, 2, 2, blue, black);

  screen_buf_put(sb, 0, 0, 'R');
  screen_buf_put(sb, 1, 1, 'G');
  screen_buf_put(sb, 2, 2, 'B');

  assert(screen_buf_cell(sb, 0, 0)->ch == 'R');
  assert(screen_buf_cell(sb, 1, 1)->ch == 'G');
  assert(screen_buf_cell(sb, 2, 2)->ch == 'B');

  // Erase row 1 without reset
  screen_buf_clear_row(sb, 1, false);
  assert(screen_buf_cell(sb, 1, 1)->ch == ' ');       // cleared
  assert(screen_buf_cell(sb, 1, 1)->fg[0] == 0);       // green preserved
  assert(screen_buf_cell(sb, 1, 1)->fg[1] == 255);

  // Erase row 2 with reset
  screen_buf_clear_row(sb, 2, true);
  assert(screen_buf_cell(sb, 2, 2)->fg[0] == 220);     // default

  // Row 0 untouched
  assert(screen_buf_cell(sb, 0, 0)->ch == 'R');
  assert(screen_buf_cell(sb, 0, 0)->fg[0] == 255);

  screen_buf_free(sb);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  printf("screen buffer tests:\n");

  TEST("new/free");                   test_new_free();           PASS();
  TEST("new invalid args");           test_new_invalid_args();   PASS();
  TEST("free NULL");                  test_free_null();          PASS();
  TEST("put and cell");               test_put_and_cell();       PASS();
  TEST("put/cell NULL");              test_put_cell_null();      PASS();
  TEST("apply SGR");                  test_apply_sgr();          PASS();
  TEST("apply SGR NULL");             test_apply_sgr_null();     PASS();
  TEST("apply SGR OOB");              test_apply_sgr_oob();      PASS();
  TEST("clear row");                  test_clear_row();          PASS();
  TEST("clear row edge");             test_clear_row_edge();     PASS();
  TEST("erase display mode 0");       test_erase_display();      PASS();
  TEST("erase display mode 1");       test_erase_display_mode1();PASS();
  TEST("erase display mode 2");       test_erase_display_mode2();PASS();
  TEST("erase display mode 2 reset"); test_erase_display_mode2_reset();PASS();
  TEST("erase display NULL");         test_erase_display_null(); PASS();
  TEST("erase line");                 test_erase_line();         PASS();
  TEST("erase line mode 1");          test_erase_line_mode1();   PASS();
  TEST("erase line edge");            test_erase_line_edge();    PASS();
  TEST("put preserves attrs");        test_put_preserves_attrs();PASS();
  TEST("multi-cell ops");             test_multi_cell_operations();PASS();

  printf("\n%d / %d tests passed.\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
