// ---------------------------------------------------------------------------
// Test suite for the scrollback buffer (scrollback.h / scrollback.c)
//
// Build:  cc -std=c2y -Wall -Wextra -Werror -Iinclude \
//             src/scrollback.c src/test_scrollback.c -o test_scrollback
// Run:    ./test_scrollback
// ---------------------------------------------------------------------------

#include "scrollback.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Minimal test framework
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

#define ASSERT_STR_EQ(a, b, len, msg)                                     \
  do {                                                                    \
    if (memcmp((a), (b), (len)) != 0) {                                   \
      printf("\n    ASSERT_STR_EQ: %s\n    File: %s:%d\n", msg,            \
             __FILE__, __LINE__);                                         \
      return false;                                                       \
    }                                                                     \
  } while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#define COLS 100
#define CAP  10

// Fill `buf` with a predictable pattern for the given line number.
static void fill_line(char *buf, int line, int cols) {
  for (int i = 0; i < cols; i++)
    buf[i] = (char)(' ' + (line + i) % 95);
}

// Check that line matches the expected pattern for the given line number.
static int check_line(const char *buf, int line, int cols) {
  for (int i = 0; i < cols; i++) {
    if (buf[i] != (char)(' ' + (line + i) % 95))
      return 0;
  }
  return 1;
}

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------

static bool test_create_destroy(void) {
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create should succeed");
  ASSERT_INT_EQ(scrollback_count(sb), 0, "empty after create");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), 0, "total = 0");
  scrollback_destroy(sb);

  // NULL destroy is a no-op.
  scrollback_destroy(NULL);
  return true;
}

static bool test_create_null_on_bad_args(void) {
  scrollback_t *sb;
  sb = scrollback_create(0, COLS);
  ASSERT(sb == NULL, "capacity 0 returns NULL");
  sb = scrollback_create(CAP, 0);
  ASSERT(sb == NULL, "cols 0 returns NULL");
  sb = scrollback_create(-1, COLS);
  ASSERT(sb == NULL, "negative capacity returns NULL");
  return true;
}

static bool test_push_and_get(void) {
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  char line[COLS];
  for (int i = 0; i < 5; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  ASSERT_INT_EQ(scrollback_count(sb), 5, "count = 5");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), 5, "total = 5");

  // Get most recent (index 0 should be line 4).
  char out[COLS];
  ASSERT(scrollback_get(sb, 0, out, COLS), "get index 0");
  ASSERT(check_line(out, 4, COLS), "most recent = line 4");

  ASSERT(scrollback_get(sb, 4, out, COLS), "get index 4");
  ASSERT(check_line(out, 0, COLS), "oldest = line 0");

  // Out of range
  ASSERT(!scrollback_get(sb, 5, out, COLS), "index 5 out of range");
  ASSERT(!scrollback_get(sb, -1, out, COLS), "negative index out of range");

  scrollback_destroy(sb);
  return true;
}

static bool test_wrap_around(void) {
  // Push more than CAP lines to test wrap-around.
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  char line[COLS];
  int total = CAP + 5;
  for (int i = 0; i < total; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  // After pushing CAP+5, only CAP entries remain.
  ASSERT_INT_EQ(scrollback_count(sb), CAP, "count capped at capacity");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), total, "total = CAP+5");

  // Most recent (index 0) = line total-1.
  char out[COLS];
  ASSERT(scrollback_get(sb, 0, out, COLS), "get most recent");
  ASSERT(check_line(out, total - 1, COLS), "most recent = last pushed");

  // Oldest (index CAP-1) = line total-CAP.
  ASSERT(scrollback_get(sb, CAP - 1, out, COLS), "get oldest");
  ASSERT(check_line(out, total - CAP, COLS), "oldest = first retained");

  scrollback_destroy(sb);
  return true;
}

static bool test_clear(void) {
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  char line[COLS];
  for (int i = 0; i < 5; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  ASSERT_INT_EQ(scrollback_count(sb), 5, "count = 5 before clear");
  scrollback_clear(sb);
  ASSERT_INT_EQ(scrollback_count(sb), 0, "count = 0 after clear");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), 5, "total preserved after clear");

  // Push again after clear.
  fill_line(line, 99, COLS);
  scrollback_push(sb, line);
  ASSERT_INT_EQ(scrollback_count(sb), 1, "count = 1 after re-push");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), 6, "total incremented");

  char out[COLS];
  ASSERT(scrollback_get(sb, 0, out, COLS), "get after re-push");
  ASSERT(check_line(out, 99, COLS), "line 99");

  scrollback_destroy(sb);
  return true;
}

static bool test_reset(void) {
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  char line[COLS];
  for (int i = 0; i < 5; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  scrollback_reset(sb);
  ASSERT_INT_EQ(scrollback_count(sb), 0, "count = 0 after reset");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), 0, "total = 0 after reset");
  scrollback_destroy(sb);
  return true;
}

static bool test_null_safety(void) {
  // All public functions should handle NULL gracefully.
  ASSERT_INT_EQ(scrollback_count(NULL), 0, "count(NULL) = 0");
  ASSERT_INT_EQ(scrollback_total_pushed(NULL), 0, "total(NULL) = 0");
  scrollback_push(NULL, NULL);
  scrollback_clear(NULL);
  scrollback_reset(NULL);

  char out[COLS];
  ASSERT(!scrollback_get(NULL, 0, out, COLS), "get(NULL) fails");
  ASSERT(!scrollback_get(NULL, 0, NULL, COLS), "get(NULL, NULL) fails");
  scrollback_destroy(NULL);
  return true;
}

static bool test_exact_capacity(void) {
  // Push exactly CAP lines, then verify all are retrievable.
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  char line[COLS];
  for (int i = 0; i < CAP; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  ASSERT_INT_EQ(scrollback_count(sb), CAP, "count = CAP");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), CAP, "total = CAP");

  char out[COLS];
  for (int i = 0; i < CAP; i++) {
    ASSERT(scrollback_get(sb, i, out, COLS), "get index");
    ASSERT(check_line(out, CAP - 1 - i, COLS), "line order check");
  }

  scrollback_destroy(sb);
  return true;
}

static bool test_partial_out_buffer(void) {
  // get() with output buffer smaller than cols should copy only what fits.
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  char line[COLS];
  fill_line(line, 42, COLS);
  scrollback_push(sb, line);

  char small[5];
  ASSERT(scrollback_get(sb, 0, small, 5), "get with small buffer");
  ASSERT_INT_EQ(memcmp(small, line, 5), 0, "first 5 bytes match");

  scrollback_destroy(sb);
  return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  printf("scrollback test suite\n");
  printf("=====================\n\n");

  TEST(create_destroy);
  TEST(create_null_on_bad_args);
  TEST(push_and_get);
  TEST(wrap_around);
  TEST(clear);
  TEST(reset);
  TEST(null_safety);
  TEST(exact_capacity);
  TEST(partial_out_buffer);

  printf("\n");
  printf("=================\n");
  printf("results: %d / %d passed", tests_passed, tests_run);
  if (tests_failed > 0)
    printf(", %d FAILED", tests_failed);
  printf("\n");

  return tests_failed > 0 ? 1 : 0;
}
