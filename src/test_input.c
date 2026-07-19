// ---------------------------------------------------------------------------
// Test suite for keyboard input (input.c / input.h)
//
// Tests the key_to_seq() function which converts raylib key codes +
// modifier flags into terminal escape sequences.
//
// Build:  cc -std=c2y -Wall -Wextra -Werror -Iinclude \
//             src/input.c src/test_input.c -o test_input -lraylib
// Run:    ./test_input
// ---------------------------------------------------------------------------

#include "input.h"
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

#define ASSERT_SEQ_EQ(seq, len, expected, exp_len, msg)                   \
  do {                                                                    \
    if ((int)(len) != (int)(exp_len)) {                                    \
      printf("\n    ASSERT_SEQ_EQ: %s: len=%d expected=%d\n"              \
             "    File: %s:%d\n",                                         \
             msg, (int)(len), (int)(exp_len), __FILE__, __LINE__);        \
      return false;                                                       \
    }                                                                     \
    if (memcmp((seq), (expected), (exp_len)) != 0) {                      \
      printf("\n    ASSERT_SEQ_EQ: %s: data mismatch\n"                   \
             "    File: %s:%d\n",                                         \
             msg, __FILE__, __LINE__);                                    \
      return false;                                                       \
    }                                                                     \
  } while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint8_t out[INPUT_MAX_SEQ];
static uint8_t esc_a[]        = {0x1B, '[', 'A'};
static uint8_t esc_b[]        = {0x1B, '[', 'B'};
static uint8_t esc_c[]        = {0x1B, '[', 'C'};
static uint8_t esc_d[]        = {0x1B, '[', 'D'};
static uint8_t esc_h[]        = {0x1B, '[', 'H'};
static uint8_t esc_f[]        = {0x1B, '[', 'F'};
static uint8_t esc_op[]       = {0x1B, 'O', 'P'};
static uint8_t esc_oq[]       = {0x1B, 'O', 'Q'};
static uint8_t esc_or[]       = {0x1B, 'O', 'R'};
static uint8_t esc_os[]       = {0x1B, 'O', 'S'};
static uint8_t esc_5_tilde[]  = {0x1B, '[', '5', '~'};
static uint8_t esc_6_tilde[]  = {0x1B, '[', '6', '~'};
static uint8_t esc_2_tilde[]  = {0x1B, '[', '2', '~'};
static uint8_t esc_3_tilde[]  = {0x1B, '[', '3', '~'};
static uint8_t esc_15_tilde[] = {0x1B, '[', '1', '5', '~'};
static uint8_t esc_17_tilde[] = {0x1B, '[', '1', '7', '~'};
static uint8_t esc_18_tilde[] = {0x1B, '[', '1', '8', '~'};
static uint8_t esc_19_tilde[] = {0x1B, '[', '1', '9', '~'};
static uint8_t esc_20_tilde[] = {0x1B, '[', '2', '0', '~'};
static uint8_t esc_21_tilde[] = {0x1B, '[', '2', '1', '~'};
static uint8_t esc_23_tilde[] = {0x1B, '[', '2', '3', '~'};
static uint8_t esc_24_tilde[] = {0x1B, '[', '2', '4', '~'};
static uint8_t esc_1_5_a[]    = {0x1B, '[', '1', ';', '5', 'A'};
static uint8_t esc_1_5_b[]    = {0x1B, '[', '1', ';', '5', 'B'};
static uint8_t esc_1_5_c[]    = {0x1B, '[', '1', ';', '5', 'C'};
static uint8_t esc_1_5_d[]    = {0x1B, '[', '1', ';', '5', 'D'};
static uint8_t double_esc_a[] = {0x1B, 0x1B, '[' , 'A'};

// ---------------------------------------------------------------------------
// Ctrl + letter tests
// ---------------------------------------------------------------------------

static bool test_ctrl_a_to_z(void) {
  for (int k = KEY_A; k <= KEY_Z; k++) {
    int len = key_to_seq(k, false, true, false, out);
    ASSERT_INT_EQ(len, 1, "Ctrl+letter is 1 byte");
    ASSERT_INT_EQ(out[0], (uint8_t)(k - KEY_A + 1),
                  "Ctrl+A..Z mapping");
  }
  return true;
}

// ---- Plain special keys ----

static bool test_enter(void) {
  int len = key_to_seq(KEY_ENTER, false, false, false, out);
  ASSERT_INT_EQ(len, 1, "Enter = 1 byte");
  ASSERT_INT_EQ(out[0], '\r', "Enter = CR");
  return true;
}

static bool test_kp_enter(void) {
  int len = key_to_seq(KEY_KP_ENTER, false, false, false, out);
  ASSERT_INT_EQ(len, 1, "KP Enter = 1 byte");
  ASSERT_INT_EQ(out[0], '\r', "KP Enter = CR");
  return true;
}

static bool test_tab(void) {
  int len = key_to_seq(KEY_TAB, false, false, false, out);
  ASSERT_INT_EQ(len, 1, "Tab = 1 byte");
  ASSERT_INT_EQ(out[0], '\t', "Tab = HT");
  return true;
}

static bool test_backspace(void) {
  int len = key_to_seq(KEY_BACKSPACE, false, false, false, out);
  ASSERT_INT_EQ(len, 1, "Backspace = 1 byte");
  ASSERT_INT_EQ(out[0], 0x7F, "Backspace = DEL");
  return true;
}

static bool test_escape(void) {
  int len = key_to_seq(KEY_ESCAPE, false, false, false, out);
  ASSERT_INT_EQ(len, 1, "Escape = 1 byte");
  ASSERT_INT_EQ(out[0], 0x1B, "Escape = ESC");
  return true;
}

static bool test_escape_with_alt(void) {
  // Alt+Escape: alt handler prepends ESC, then Escape with !alt gives nothing.
  // Result: just the ESC prefix (1 byte).
  int len = key_to_seq(KEY_ESCAPE, false, false, true, out);
  ASSERT_INT_EQ(len, 1, "Alt+Escape = ESC prefix");
  ASSERT_INT_EQ(out[0], 0x1B, "Alt+Escape = 0x1B");
  return true;
}

// ---- Arrow keys ----

static bool test_up(void) {
  int len = key_to_seq(KEY_UP, false, false, false, out);
  ASSERT_SEQ_EQ(out, len, esc_a, sizeof(esc_a), "Up arrow");
  return true;
}

static bool test_down(void) {
  int len = key_to_seq(KEY_DOWN, false, false, false, out);
  ASSERT_SEQ_EQ(out, len, esc_b, sizeof(esc_b), "Down arrow");
  return true;
}

static bool test_right(void) {
  int len = key_to_seq(KEY_RIGHT, false, false, false, out);
  ASSERT_SEQ_EQ(out, len, esc_c, sizeof(esc_c), "Right arrow");
  return true;
}

static bool test_left(void) {
  int len = key_to_seq(KEY_LEFT, false, false, false, out);
  ASSERT_SEQ_EQ(out, len, esc_d, sizeof(esc_d), "Left arrow");
  return true;
}

// ---- Ctrl + arrow ----

static bool test_ctrl_up(void) {
  int len = key_to_seq(KEY_UP, false, true, false, out);
  ASSERT_SEQ_EQ(out, len, esc_1_5_a, sizeof(esc_1_5_a), "Ctrl+Up");
  return true;
}

static bool test_ctrl_down(void) {
  int len = key_to_seq(KEY_DOWN, false, true, false, out);
  ASSERT_SEQ_EQ(out, len, esc_1_5_b, sizeof(esc_1_5_b), "Ctrl+Down");
  return true;
}

static bool test_ctrl_right(void) {
  int len = key_to_seq(KEY_RIGHT, false, true, false, out);
  ASSERT_SEQ_EQ(out, len, esc_1_5_c, sizeof(esc_1_5_c), "Ctrl+Right");
  return true;
}

static bool test_ctrl_left(void) {
  int len = key_to_seq(KEY_LEFT, false, true, false, out);
  ASSERT_SEQ_EQ(out, len, esc_1_5_d, sizeof(esc_1_5_d), "Ctrl+Left");
  return true;
}

// ---- Alt + arrow (double ESC prefix) ----

static bool test_alt_up(void) {
  int len = key_to_seq(KEY_UP, false, false, true, out);
  ASSERT_SEQ_EQ(out, len, double_esc_a, sizeof(double_esc_a), "Alt+Up");
  return true;
}

static bool test_alt_down(void) {
  // Alt+Down: ESC + ESC[B
  uint8_t exp[] = {0x1B, 0x1B, '[', 'B'};
  int len = key_to_seq(KEY_DOWN, false, false, true, out);
  ASSERT_SEQ_EQ(out, len, exp, sizeof(exp), "Alt+Down");
  return true;
}

// ---- Home / End ----

static bool test_home(void) {
  int len = key_to_seq(KEY_HOME, false, false, false, out);
  ASSERT_SEQ_EQ(out, len, esc_h, sizeof(esc_h), "Home");
  return true;
}

static bool test_end(void) {
  int len = key_to_seq(KEY_END, false, false, false, out);
  ASSERT_SEQ_EQ(out, len, esc_f, sizeof(esc_f), "End");
  return true;
}

// ---- Page Up / Page Down ----

static bool test_page_up(void) {
  int len = key_to_seq(KEY_PAGE_UP, false, false, false, out);
  ASSERT_SEQ_EQ(out, len, esc_5_tilde, sizeof(esc_5_tilde), "PageUp");
  return true;
}

static bool test_page_down(void) {
  int len = key_to_seq(KEY_PAGE_DOWN, false, false, false, out);
  ASSERT_SEQ_EQ(out, len, esc_6_tilde, sizeof(esc_6_tilde), "PageDown");
  return true;
}

// ---- Insert / Delete ----

static bool test_insert(void) {
  int len = key_to_seq(KEY_INSERT, false, false, false, out);
  ASSERT_SEQ_EQ(out, len, esc_2_tilde, sizeof(esc_2_tilde), "Insert");
  return true;
}

static bool test_delete(void) {
  int len = key_to_seq(KEY_DELETE, false, false, false, out);
  ASSERT_SEQ_EQ(out, len, esc_3_tilde, sizeof(esc_3_tilde), "Delete");
  return true;
}

// ---- Function keys ----

static bool test_f1_to_f4(void) {
  // F1-F4: \x1BO[P..S]
  struct { int key; uint8_t *exp; int exp_len; } cases[] = {
    {KEY_F1, esc_op, 3},
    {KEY_F2, esc_oq, 3},
    {KEY_F3, esc_or, 3},
    {KEY_F4, esc_os, 3},
  };
  for (int i = 0; i < 4; i++) {
    int len = key_to_seq(cases[i].key, false, false, false, out);
    ASSERT_SEQ_EQ(out, len, cases[i].exp, (size_t)cases[i].exp_len, "F1-F4");
  }
  return true;
}

static bool test_f5_to_f12(void) {
  // F5-F12: \x1B[15~ etc.
  struct { int key; uint8_t *exp; int exp_len; } cases[] = {
    {KEY_F5,  esc_15_tilde, 5},
    {KEY_F6,  esc_17_tilde, 5},
    {KEY_F7,  esc_18_tilde, 5},
    {KEY_F8,  esc_19_tilde, 5},
    {KEY_F9,  esc_20_tilde, 5},
    {KEY_F10, esc_21_tilde, 5},
    {KEY_F11, esc_23_tilde, 5},
    {KEY_F12, esc_24_tilde, 5},
  };
  for (int i = 0; i < 8; i++) {
    int len = key_to_seq(cases[i].key, false, false, false, out);
    ASSERT_SEQ_EQ(out, len, cases[i].exp, (size_t)cases[i].exp_len, "F5-F12");
  }
  return true;
}

// ---- Keypad keys ----

static bool test_kp_digits(void) {
  int keys[] = {KEY_KP_0, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4,
                KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9};
  for (int i = 0; i < 10; i++) {
    int len = key_to_seq(keys[i], false, false, false, out);
    ASSERT_INT_EQ(len, 1, "KP digit = 1 byte");
    ASSERT_INT_EQ(out[0], (uint8_t)('0' + i), "KP digit value");
  }
  return true;
}

static bool test_kp_operators(void) {
  struct { int key; uint8_t expected; } cases[] = {
    {KEY_KP_DECIMAL,  '.'},
    {KEY_KP_DIVIDE,   '/'},
    {KEY_KP_MULTIPLY, '*'},
    {KEY_KP_SUBTRACT, '-'},
    {KEY_KP_ADD,      '+'},
  };
  for (int i = 0; i < 5; i++) {
    int len = key_to_seq(cases[i].key, false, false, false, out);
    ASSERT_INT_EQ(len, 1, "KP operator = 1 byte");
    ASSERT_INT_EQ(out[0], cases[i].expected, "KP operator value");
  }
  return true;
}

// ---- Alt + letter (ESC prefix) ----

static bool test_alt_letter(void) {
  // Alt+A: ESC prefix is prepended (alt && !ctrl), but KEY_A is not in
  // the switch so it falls through to default.  Result: just the ESC prefix.
  int len = key_to_seq(KEY_A, false, false, true, out);
  ASSERT_INT_EQ(len, 1, "Alt+A = ESC prefix");
  ASSERT_INT_EQ(out[0], 0x1B, "Alt+A = 0x1B");
  return true;
}

// ---- Alt + Ctrl combination ----

static bool test_alt_ctrl_non_printable(void) {
  // Alt+C+Ctrl: ctrl is checked first, alt is skipped when ctrl is true.
  // This produces Ctrl+C = 0x03.
  int len = key_to_seq(KEY_C, false, true, true, out);
  ASSERT_INT_EQ(len, 1, "Ctrl+C with Alt = Ctrl+C (Alt ignored)");
  ASSERT_INT_EQ(out[0], 0x03, "Ctrl+C = 0x03");
  return true;
}

// ---- Unmapped key ----

static bool test_unmapped_key(void) {
  // Use a key code that's not handled (e.g. KEY_PAUSE = 284 on some systems,
  // or just use 0).
  int len = key_to_seq(0, false, false, false, out);
  ASSERT_INT_EQ(len, 0, "unmapped key returns 0");
  return true;
}

// ---- Shift modifier (should be ignored for non-printable) ----

static bool test_shift_arrow(void) {
  // Shift currently doesn't affect arrow sequences (see comment in code).
  int len = key_to_seq(KEY_UP, true, false, false, out);
  ASSERT_SEQ_EQ(out, len, esc_a, sizeof(esc_a), "Shift+Up = same as Up");
  return true;
}

// ---- Buffer overflow protection ----

static bool test_sequence_does_not_exceed_max(void) {
  // The longest sequence is Ctrl+Arrow = 6 bytes (\x1B[1;5A).
  // Ensure we never write more than INPUT_MAX_SEQ-1 bytes.
  for (int k = 0; k < 512; k++) {
    int len = key_to_seq(k, false, false, false, out);
    ASSERT(len <= INPUT_MAX_SEQ, "sequence does not exceed max");
    (void)len;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  printf("input test suite\n");
  printf("================\n\n");

  // Ctrl + letter
  TEST(ctrl_a_to_z);

  // Plain special keys
  TEST(enter);
  TEST(kp_enter);
  TEST(tab);
  TEST(backspace);
  TEST(escape);
  TEST(escape_with_alt);

  // Arrow keys
  TEST(up);
  TEST(down);
  TEST(right);
  TEST(left);
  TEST(ctrl_up);
  TEST(ctrl_down);
  TEST(ctrl_right);
  TEST(ctrl_left);
  TEST(alt_up);
  TEST(alt_down);

  // Navigation keys
  TEST(home);
  TEST(end);
  TEST(page_up);
  TEST(page_down);
  TEST(insert);
  TEST(delete);

  // Function keys
  TEST(f1_to_f4);
  TEST(f5_to_f12);

  // Keypad
  TEST(kp_digits);
  TEST(kp_operators);

  // Modifier combinations
  TEST(alt_letter);
  TEST(alt_ctrl_non_printable);
  TEST(unmapped_key);
  TEST(shift_arrow);
  TEST(sequence_does_not_exceed_max);

  printf("\n");
  printf("================\n");
  printf("results: %d / %d passed", tests_passed, tests_run);
  if (tests_failed > 0)
    printf(", %d FAILED", tests_failed);
  printf("\n");

  return tests_failed > 0 ? 1 : 0;
}
