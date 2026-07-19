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

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#define COLS 100
#define CAP  10

// Fill `buf` with a predictable codepoint pattern for the given line number.
static void fill_line(int *buf, int line, int cols) {
  for (int i = 0; i < cols; i++)
    buf[i] = ' ' + (line + i) % 95;
}

// Check that line matches the expected pattern for the given line number.
static int check_line(const int *buf, int line, int cols) {
  for (int i = 0; i < cols; i++) {
    if (buf[i] != ' ' + (line + i) % 95)
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

  int line[COLS];
  for (int i = 0; i < 5; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  ASSERT_INT_EQ(scrollback_count(sb), 5, "count = 5");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), 5, "total = 5");

  // Get most recent (index 0 should be line 4).
  int out[COLS];
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

  int line[COLS];
  int total = CAP + 5;
  for (int i = 0; i < total; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  // After pushing CAP+5, only CAP entries remain.
  ASSERT_INT_EQ(scrollback_count(sb), CAP, "count capped at capacity");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), total, "total = CAP+5");

  // Most recent (index 0) = line total-1.
  int out[COLS];
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

  int line[COLS];
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

  int out[COLS];
  ASSERT(scrollback_get(sb, 0, out, COLS), "get after re-push");
  ASSERT(check_line(out, 99, COLS), "line 99");

  scrollback_destroy(sb);
  return true;
}

static bool test_reset(void) {
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  int line[COLS];
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

  int out[COLS];
  ASSERT(!scrollback_get(NULL, 0, out, COLS), "get(NULL) fails");
  ASSERT(!scrollback_get(NULL, 0, NULL, COLS), "get(NULL, NULL) fails");
  scrollback_destroy(NULL);
  return true;
}

static bool test_exact_capacity(void) {
  // Push exactly CAP lines, then verify all are retrievable.
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  int line[COLS];
  for (int i = 0; i < CAP; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  ASSERT_INT_EQ(scrollback_count(sb), CAP, "count = CAP");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), CAP, "total = CAP");

  int out[COLS];
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

  int line[COLS];
  fill_line(line, 42, COLS);
  scrollback_push(sb, line);

  int small[5];
  ASSERT(scrollback_get(sb, 0, small, 5), "get with small buffer");
  ASSERT_INT_EQ(memcmp(small, line, 5 * sizeof(int)), 0, "first 5 codepoints match");

  scrollback_destroy(sb);
  return true;
}

// ---------------------------------------------------------------------------
// Additional edge-case and viewport-mapping tests
// ---------------------------------------------------------------------------

static bool test_capacity_one(void) {
  // Edge case: buffer with capacity 1.
  scrollback_t *sb = scrollback_create(1, COLS);
  ASSERT(sb != NULL, "create cap=1");

  int line[COLS];
  fill_line(line, 0, COLS);
  scrollback_push(sb, line);
  ASSERT_INT_EQ(scrollback_count(sb), 1, "count = 1 after 1 push");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), 1, "total = 1");

  int out[COLS];
  ASSERT(scrollback_get(sb, 0, out, COLS), "get index 0");
  ASSERT(check_line(out, 0, COLS), "line 0");

  // Push a second line – should overwrite the first.
  fill_line(line, 99, COLS);
  scrollback_push(sb, line);
  ASSERT_INT_EQ(scrollback_count(sb), 1, "count = 1 after 2 pushes");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), 2, "total = 2");
  ASSERT(scrollback_get(sb, 0, out, COLS), "get index 0 after overwrite");
  ASSERT(check_line(out, 99, COLS), "most recent = line 99");

  scrollback_destroy(sb);
  return true;
}

static bool test_cols_one(void) {
  // Edge case: buffer with a single column.
  scrollback_t *sb = scrollback_create(5, 1);
  ASSERT(sb != NULL, "create cols=1");

  int line_a[] = {'A'};
  int line_b[] = {'B'};
  int line_c[] = {'C'};
  scrollback_push(sb, line_a);
  scrollback_push(sb, line_b);
  scrollback_push(sb, line_c);

  ASSERT_INT_EQ(scrollback_count(sb), 3, "count = 3");

  int out[4];
  ASSERT(scrollback_get(sb, 0, out, 4), "get index 0");
  ASSERT_INT_EQ(out[0], 'C', "most recent = C");
  ASSERT(scrollback_get(sb, 2, out, 4), "get index 2");
  ASSERT_INT_EQ(out[0], 'A', "oldest = A");

  scrollback_destroy(sb);
  return true;
}

static bool test_stress_sequential(void) {
  // Push 10× capacity and verify every stored line.
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  int total = CAP * 10;
  int line[COLS];
  for (int i = 0; i < total; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  ASSERT_INT_EQ(scrollback_count(sb), CAP, "count capped at CAP");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), total, "total = 10*CAP");

  // Verify all CAP stored lines: oldest (index CAP-1) to newest (index 0).
  int out[COLS];
  for (int i = 0; i < CAP; i++) {
    int expected_line = total - CAP + i;   // oldest retained line + i
    ASSERT(scrollback_get(sb, CAP - 1 - i, out, COLS), "get index");
    ASSERT(check_line(out, expected_line, COLS), "stress line check");
  }

  scrollback_destroy(sb);
  return true;
}

static bool test_interleaved_push_get(void) {
  // Interleave pushes and gets to verify ordering is consistent.
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  int line[COLS];
  int out[COLS];

  // Push 3 lines: 0, 1, 2
  for (int i = 0; i < 3; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  // Read-back line 1 (index 1 should be the second push, line 1).
  ASSERT(scrollback_get(sb, 1, out, COLS), "get after 3 pushes");
  ASSERT(check_line(out, 1, COLS), "line 1 at index 1");

  // Push 2 more lines: 3, 4  (now have 0..4 stored)
  for (int i = 3; i < 5; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  ASSERT(scrollback_get(sb, 0, out, COLS), "get most recent after push");
  ASSERT(check_line(out, 4, COLS), "most recent = line 4");
  ASSERT(scrollback_get(sb, 4, out, COLS), "get oldest");
  ASSERT(check_line(out, 0, COLS), "oldest = line 0");

  scrollback_destroy(sb);
  return true;
}

static bool test_viewport_indexing(void) {
  // Simulate the viewport coordinate mapping used in main.c.
  //
  // For a row r with scroll_offset > 0:
  //   sb_idx = scroll_offset - 1 - r   (which scrollback line to show)
  //
  // This test verifies that fetching via scrollback_get with these
  // computed indices returns the expected content.

  int cap = 20;
  scrollback_t *sb = scrollback_create(cap, COLS);
  ASSERT(sb != NULL, "create");

  int line[COLS];
  int total = 15;  // push 15 distinct lines (lines 0..14)
  for (int i = 0; i < total; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  int out[COLS];

  // Scenario 1: scroll_offset = 5 (showing 5 scrollback lines at top).
  //   Row 0 -> sb_idx = 4 -> scrollback_get(4) = line total-1-4 = line 10
  //   Row 4 -> sb_idx = 0 -> scrollback_get(0) = line total-1   = line 14
  int scroll_offset = 5;
  for (int r = 0; r < scroll_offset; r++) {
    int sb_idx = scroll_offset - 1 - r;
    int expected_line = total - 1 - sb_idx;
    ASSERT(scrollback_get(sb, sb_idx, out, COLS), "vp index get");
    ASSERT(check_line(out, expected_line, COLS), "vp index content");
  }

  // Scenario 2: scroll_offset = 15 (showing all scrollback lines).
  //   Row  0 -> sb_idx = 14 -> line 0
  //   Row 14 -> sb_idx =  0 -> line 14
  scroll_offset = 15;
  for (int r = 0; r < scroll_offset; r++) {
    int sb_idx = scroll_offset - 1 - r;
    int expected_line = total - 1 - sb_idx;
    ASSERT(scrollback_get(sb, sb_idx, out, COLS), "vp full get");
    ASSERT(check_line(out, expected_line, COLS), "vp full content");
  }

  scrollback_destroy(sb);
  return true;
}

static bool test_offset_clamping(void) {
  // Verify that scrollback_get correctly rejects out-of-range indices
  // that would correspond to scroll_offset > scrollback_count.
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  int line[COLS];
  for (int i = 0; i < 3; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  // Only indices 0..2 are valid (count = 3).
  int out[COLS];
  ASSERT(scrollback_get(sb, 0, out, COLS),  "index 0 valid");
  ASSERT(scrollback_get(sb, 2, out, COLS),  "index 2 valid");
  ASSERT(!scrollback_get(sb, 3, out, COLS), "index 3 out of range");
  ASSERT(!scrollback_get(sb, 100, out, COLS), "index 100 out of range");

  // Fill to capacity and verify the boundary again.
  for (int i = 3; i < CAP + 5; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }
  // Now count = CAP, so valid indices are 0..CAP-1.
  ASSERT(!scrollback_get(sb, CAP, out, COLS),     "index CAP out of range");
  ASSERT(scrollback_get(sb, CAP - 1, out, COLS),  "index CAP-1 valid");

  scrollback_destroy(sb);
  return true;
}

static bool test_get_after_clear_and_repush(void) {
  // Clear the buffer, push fresh content, and verify retrieval.
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  int line[COLS];
  int out[COLS];

  // Push a few lines then clear.
  for (int i = 0; i < 5; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }
  scrollback_clear(sb);

  // Push fresh content (lines 10, 11, 12).
  for (int i = 10; i < 13; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  ASSERT_INT_EQ(scrollback_count(sb), 3, "count = 3 after clear+repush");
  ASSERT(scrollback_get(sb, 0, out, COLS), "get index 0");
  ASSERT(check_line(out, 12, COLS), "most recent = line 12");
  ASSERT(scrollback_get(sb, 2, out, COLS), "get index 2");
  ASSERT(check_line(out, 10, COLS), "oldest = line 10");

  scrollback_destroy(sb);
  return true;
}

static bool test_many_wrap_arounds(void) {
  // Push 3× capacity in multiple passes to exercise repeated wraps.
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  int line[COLS];
  int out[COLS];
  int total = CAP * 3;

  for (int i = 0; i < total; i++) {
    fill_line(line, i, COLS);
    scrollback_push(sb, line);
  }

  ASSERT_INT_EQ(scrollback_count(sb), CAP, "count = CAP after 3 wraps");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), total, "total = 3*CAP");

  // Verify every stored line.
  int oldest_expected = total - CAP;  // first line that survives
  for (int i = 0; i < CAP; i++) {
    ASSERT(scrollback_get(sb, CAP - 1 - i, out, COLS), "wrap get");
    ASSERT(check_line(out, oldest_expected + i, COLS), "wrap content");
  }

  scrollback_destroy(sb);
  return true;
}

static bool test_push_null_line(void) {
  // scrollback_push with NULL line should be a safe no-op.
  scrollback_t *sb = scrollback_create(CAP, COLS);
  ASSERT(sb != NULL, "create");

  // Normal push first.
  int line[COLS];
  fill_line(line, 42, COLS);
  scrollback_push(sb, line);
  ASSERT_INT_EQ(scrollback_count(sb), 1, "count = 1 after normal push");

  // NULL push should not change state.
  scrollback_push(sb, NULL);
  ASSERT_INT_EQ(scrollback_count(sb), 1, "count unchanged after NULL push");
  ASSERT_INT_EQ(scrollback_total_pushed(sb), 1, "total unchanged after NULL push");

  int out[COLS];
  ASSERT(scrollback_get(sb, 0, out, COLS), "get after NULL push");
  ASSERT(check_line(out, 42, COLS), "content preserved");

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

  // New tests for edge cases and viewport-mapping patterns.
  TEST(capacity_one);
  TEST(cols_one);
  TEST(stress_sequential);
  TEST(interleaved_push_get);
  TEST(viewport_indexing);
  TEST(offset_clamping);
  TEST(get_after_clear_and_repush);
  TEST(many_wrap_arounds);
  TEST(push_null_line);

  printf("\n");
  printf("=================\n");
  printf("results: %d / %d passed", tests_passed, tests_run);
  if (tests_failed > 0)
    printf(", %d FAILED", tests_failed);
  printf("\n");

  return tests_failed > 0 ? 1 : 0;
}
