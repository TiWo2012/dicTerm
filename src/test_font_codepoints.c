// ---------------------------------------------------------------------------
// Test suite for font codepoint list builder (font_build_codepoint_list)
//
// This function is a pure numeric range builder — no raylib dependency.
// We test it by compiling font.c and relying on the linker to resolve the
// unused raylib symbols (which we never call).
//
// Build:  cc -std=c2y -Wall -Wextra -Werror -Iinclude \
//             src/font.c src/test_font_codepoints.c \
//             -o test_font_codepoints -lraylib
// Run:    ./test_font_codepoints
// ---------------------------------------------------------------------------

#include "font.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// ---------------------------------------------------------------------------
// Expected counts
// ---------------------------------------------------------------------------

// ASCII printable: 0x20..0x7E = 95 codepoints
#define ASCII_COUNT (FONT_ASCII_MAX - FONT_ASCII_MIN + 1)

// NERD ranges: sum of (end - start + 1) for each range
static int calc_nerd_count(void) {
  int total = 0;
  for (int r = 0; r < FONT_NERD_RANGES_COUNT; r++) {
    total += FONT_NERD_RANGES[r][1] - FONT_NERD_RANGES[r][0] + 1;
  }
  return total;
}

#define NERD_COUNT calc_nerd_count()
#define TOTAL_COUNT (ASCII_COUNT + NERD_COUNT)

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------

/// Basic ASCII-only codepoint list.
static bool test_ascii_only(void) {
  int buf[8192];
  int count = font_build_codepoint_list(buf, 8192, false);

  ASSERT_INT_EQ(count, ASCII_COUNT, "ASCII-only count");

  // Verify the range
  for (int i = 0; i < count; i++) {
    ASSERT_INT_EQ(buf[i], FONT_ASCII_MIN + i,
                  "ASCII codepoint value");
  }
  return true;
}

/// Full list including NERD ranges.
static bool test_with_nerd(void) {
  int buf[8192];
  int count = font_build_codepoint_list(buf, 8192, true);

  ASSERT_INT_EQ(count, TOTAL_COUNT, "total count with NERD");

  // First ASCII_COUNT entries are ASCII
  for (int i = 0; i < ASCII_COUNT; i++) {
    ASSERT_INT_EQ(buf[i], FONT_ASCII_MIN + i,
                  "ASCII prefix with NERD");
  }

  // Remaining entries should cover all NERD ranges in order
  int idx = ASCII_COUNT;
  for (int r = 0; r < FONT_NERD_RANGES_COUNT; r++) {
    int start = FONT_NERD_RANGES[r][0];
    int end   = FONT_NERD_RANGES[r][1];
    for (int cp = start; cp <= end; cp++) {
      ASSERT_INT_EQ(buf[idx], cp, "NERD range codepoint");
      idx++;
    }
  }
  ASSERT_INT_EQ(idx, count, "all NERD entries consumed");
  return true;
}

/// Buffer too small — should be truncated.
static bool test_small_buffer(void) {
  int buf[10];
  int count = font_build_codepoint_list(buf, 10, true);

  // Should only fill what fits
  ASSERT_INT_EQ(count, 10, "truncated to 10");
  for (int i = 0; i < 10; i++) {
    ASSERT_INT_EQ(buf[i], FONT_ASCII_MIN + i,
                  "first 10 ASCII when truncated");
  }
  return true;
}

/// Zero-size buffer.
static bool test_zero_buffer(void) {
  int buf[1];
  int count = font_build_codepoint_list(buf, 0, true);
  ASSERT_INT_EQ(count, 0, "zero-size buffer returns 0");
  return true;
}

/// Buffer exactly ASCII size (no NERD).
static bool test_exact_ascii_buffer(void) {
  int buf[ASCII_COUNT];
  int count = font_build_codepoint_list(buf, ASCII_COUNT, false);
  ASSERT_INT_EQ(count, ASCII_COUNT, "exact ASCII buffer");
  ASSERT_INT_EQ(buf[ASCII_COUNT - 1], FONT_ASCII_MAX,
                "last ASCII entry");
  return true;
}

/// Buffer exactly total size (with NERD).
static bool test_exact_total_buffer(void) {
  int buf[8192];
  int count = font_build_codepoint_list(buf, TOTAL_COUNT, true);
  ASSERT_INT_EQ(count, TOTAL_COUNT, "exact total buffer");
  return true;
}

/// Verify no duplicates within each contiguous segment.
/// The ranges are not globally sorted (NERD range 4 is 0x2500-0x27BF
/// which precedes ranges 0-2 in the 0xE000-0xFD46 region), but
/// monotonically within each range.
static bool test_no_duplicates(void) {
  int buf[8192];
  int n = font_build_codepoint_list(buf, 8192, true);
  (void)n;

  // Check each contiguous segment for monotonicity.
  // Within ASCII (0x20-0x7E): 95 entries, strictly increasing.
  for (int i = 1; i < 95; i++) {
    ASSERT(buf[i] == buf[i - 1] + 1, "ASCII is strictly increasing by 1");
  }

  // Within each NERD range: entries are strictly increasing by 1.
  int idx = 95; // after ASCII
  for (int r = 0; r < FONT_NERD_RANGES_COUNT; r++) {
    int start = FONT_NERD_RANGES[r][0];
    int end   = FONT_NERD_RANGES[r][1];
    int range_len = end - start + 1;
    for (int i = 1; i < range_len; i++) {
      ASSERT(buf[idx + i] == buf[idx + i - 1] + 1,
             "NERD range is strictly increasing by 1");
    }
    idx += range_len;
  }
  return true;
}

/// Buffer size of 1 (minimum meaningful).
static bool test_buffer_size_one(void) {
  int buf[1];
  int count = font_build_codepoint_list(buf, 1, true);
  ASSERT_INT_EQ(count, 1, "buffer size 1 returns 1");
  ASSERT_INT_EQ(buf[0], FONT_ASCII_MIN, "first codepoint is space");
  return true;
}

/// Verify that each NERD range starts and ends correctly.
static bool test_nerd_range_boundaries(void) {
  int buf[8192];
  int count = font_build_codepoint_list(buf, 8192, true);
  (void)count;

  int idx = ASCII_COUNT;
  for (int r = 0; r < FONT_NERD_RANGES_COUNT; r++) {
    int start = FONT_NERD_RANGES[r][0];
    int end   = FONT_NERD_RANGES[r][1];
    int range_len = end - start + 1;

    ASSERT_INT_EQ(buf[idx], start, "NERD range start");
    ASSERT_INT_EQ(buf[idx + range_len - 1], end, "NERD range end");
    idx += range_len;
  }
  return true;
}

/// Compare with expected NERD count.
static bool test_nerd_count_matches_ranges(void) {
  int buf[8192];
  int count = font_build_codepoint_list(buf, 8192, true);

  int nerd_part = count - ASCII_COUNT;
  ASSERT_INT_EQ(nerd_part, NERD_COUNT, "NERD count matches range sum");
  return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  printf("font codepoint list tests\n");
  printf("=========================\n\n");

  TEST(ascii_only);
  TEST(with_nerd);
  TEST(small_buffer);
  TEST(zero_buffer);
  TEST(exact_ascii_buffer);
  TEST(exact_total_buffer);
  TEST(no_duplicates);
  TEST(buffer_size_one);
  TEST(nerd_range_boundaries);
  TEST(nerd_count_matches_ranges);

  printf("\n");
  printf("=========================\n");
  printf("results: %d / %d passed", tests_passed, tests_run);
  if (tests_failed > 0)
    printf(", %d FAILED", tests_failed);
  printf("\n");

  return tests_failed > 0 ? 1 : 0;
}
