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

#define FAIL(msg) do { \
  printf("FAIL: %s\n", msg); \
  return;               \
} while(0)

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

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
    }
  }

  screen_buf_free(sb);
}

static void test_put_and_cell(void) {
  screen_buf_t *sb = screen_buf_new(5, 10);

  screen_buf_put(sb, 2, 3, 'X');
  screen_cell_t *c = screen_buf_cell(sb, 2, 3);
  assert(c != NULL);
  assert(c->ch == 'X');

  // Out of bounds should be no-op
  screen_buf_put(sb, 100, 0, '?');
  screen_buf_cell(sb, -1, 0);

  screen_buf_free(sb);
}

static void test_apply_sgr(void) {
  screen_buf_t *sb = screen_buf_new(3, 5);

  uint8_t fg[3] = {255, 0, 0};
  uint8_t bg[3] = {0, 0, 255};
  screen_buf_apply_sgr(sb, 1, 1, fg, bg);

  screen_cell_t *c = screen_buf_cell(sb, 1, 1);
  assert(c != NULL);
  assert(c->fg[0] == 255 && c->fg[1] == 0 && c->fg[2] == 0);
  assert(c->bg[0] == 0 && c->bg[1] == 0 && c->bg[2] == 255);

  screen_buf_free(sb);
}

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

  // Clear with resetting colours
  screen_buf_apply_sgr(sb, 1, 1, fg, bg);
  screen_buf_clear_row(sb, 1, true);
  c = screen_buf_cell(sb, 1, 1);
  assert(c->ch == ' ');
  assert(c->fg[0] == 220);                  // reset to default

  screen_buf_free(sb);
}

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

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  printf("screen buffer tests:\n");

  TEST("new/free");                   test_new_free();           PASS();
  TEST("put and cell");               test_put_and_cell();       PASS();
  TEST("apply SGR");                  test_apply_sgr();          PASS();
  TEST("clear row");                  test_clear_row();          PASS();
  TEST("erase display");              test_erase_display();      PASS();
  TEST("erase line");                 test_erase_line();         PASS();

  printf("\n%d / %d tests passed.\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
