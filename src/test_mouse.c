/**
 * @file test_mouse.c
 * @brief Unit tests for the mouse reporting encoder (mouse.h / mouse.c).
 *
 * Verifies DEC private mode set/reset bookkeeping and the encoding of
 * pointer events in both the legacy X10 and SGR (1006) schemes, including
 * mode gating (which events each tracking mode reports), modifier bits,
 * wheel/motion flags and coordinate handling.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "mouse.h"

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

/// @brief Encode into a NUL-terminated buffer for easy string comparison.
static int enc_str(const mouse_state_t *st, mouse_event_kind_t kind,
                   mouse_button_t btn, int col, int row,
                   bool shift, bool alt, bool ctrl, char buf[MOUSE_MAX_SEQ + 1]) {
  uint8_t out[MOUSE_MAX_SEQ];
  int n = mouse_encode(st, kind, btn, col, row, shift, alt, ctrl, out);
  memcpy(buf, out, (size_t)n);
  buf[n] = '\0';
  return n;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/// @brief mouse_set_mode maps DEC private modes and reports recognition.
static void test_set_mode(void) {
  mouse_state_t st = {0};

  assert(mouse_set_mode(&st, 1000, true));
  assert(st.track == MOUSE_TRACK_NORMAL);
  assert(mouse_set_mode(&st, 1002, true));
  assert(st.track == MOUSE_TRACK_BUTTON);
  assert(mouse_set_mode(&st, 1003, true));
  assert(st.track == MOUSE_TRACK_ANY);
  assert(mouse_set_mode(&st, 9, true));
  assert(st.track == MOUSE_TRACK_X10);

  assert(mouse_set_mode(&st, 1006, true));
  assert(st.enc == MOUSE_ENC_SGR);
  assert(mouse_set_mode(&st, 1006, false));
  assert(st.enc == MOUSE_ENC_DEFAULT);

  // Unknown modes are not recognised.
  assert(!mouse_set_mode(&st, 25, true));
  assert(!mouse_set_mode(&st, 12345, false));
}

/// @brief Disabling a mode only turns tracking off if it is the active one.
static void test_mode_reset_semantics(void) {
  mouse_state_t st = {0};

  mouse_set_mode(&st, 1000, true);        // NORMAL
  mouse_set_mode(&st, 1002, false);       // resetting a different mode: no-op
  assert(st.track == MOUSE_TRACK_NORMAL);
  mouse_set_mode(&st, 1000, false);       // reset the active one
  assert(st.track == MOUSE_TRACK_OFF);
}

/// @brief With tracking off, nothing is encoded.
static void test_off_encodes_nothing(void) {
  mouse_state_t st = {0};
  uint8_t out[MOUSE_MAX_SEQ];
  assert(mouse_encode(&st, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT, 0, 0,
                      false, false, false, out) == 0);
}

/// @brief Legacy press encoding: CSI M Cb Cx Cy with +32 bias, 1-based.
static void test_legacy_press(void) {
  mouse_state_t st = { .track = MOUSE_TRACK_NORMAL, .enc = MOUSE_ENC_DEFAULT };
  char buf[MOUSE_MAX_SEQ + 1];

  // Left button at cell (0,0): cb=0 -> 32 (' '), cx=1 -> 33 ('!'), cy=1 -> 33.
  int n = enc_str(&st, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT, 0, 0,
                  false, false, false, buf);
  assert(n == 6);
  assert(memcmp(buf, "\x1b[M !!", 6) == 0);

  // Right button at (2,3): cb=2 -> 34 ('"'), cx=3 -> 35 ('#'), cy=4 -> 36 ('$').
  n = enc_str(&st, MOUSE_EVENT_PRESS, MOUSE_BTN_RIGHT, 2, 3,
              false, false, false, buf);
  assert(n == 6);
  assert(memcmp(buf, "\x1b[M\"#$", 6) == 0);
}

/// @brief Legacy release reports button 3 (identity lost).
static void test_legacy_release(void) {
  mouse_state_t st = { .track = MOUSE_TRACK_NORMAL, .enc = MOUSE_ENC_DEFAULT };
  char buf[MOUSE_MAX_SEQ + 1];
  int n = enc_str(&st, MOUSE_EVENT_RELEASE, MOUSE_BTN_LEFT, 0, 0,
                  false, false, false, buf);
  assert(n == 6);
  // cb = 3 -> 35 ('#')
  assert(memcmp(buf, "\x1b[M#!!", 6) == 0);
}

/// @brief Modifier bits (shift=4, alt=8, ctrl=16) are OR-ed into Cb.
static void test_modifiers(void) {
  mouse_state_t st = { .track = MOUSE_TRACK_NORMAL, .enc = MOUSE_ENC_DEFAULT };
  char buf[MOUSE_MAX_SEQ + 1];
  // Left + shift + ctrl: cb = 0 + 4 + 16 = 20 -> 52 ('4').
  int n = enc_str(&st, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT, 0, 0,
                  true, false, true, buf);
  assert(n == 6);
  assert((unsigned char)buf[3] == (unsigned char)(32 + 20));
}

/// @brief Wheel encodes with the wheel flag (64); up=64, down=65.
static void test_wheel(void) {
  mouse_state_t st = { .track = MOUSE_TRACK_NORMAL, .enc = MOUSE_ENC_DEFAULT };
  char buf[MOUSE_MAX_SEQ + 1];

  enc_str(&st, MOUSE_EVENT_WHEEL_UP, MOUSE_BTN_NONE, 0, 0,
          false, false, false, buf);
  assert((unsigned char)buf[3] == (unsigned char)(32 + 64));

  enc_str(&st, MOUSE_EVENT_WHEEL_DOWN, MOUSE_BTN_NONE, 0, 0,
          false, false, false, buf);
  assert((unsigned char)buf[3] == (unsigned char)(32 + 65));
}

/// @brief SGR encoding: CSI < Cb ; Cx ; Cy M(press) / m(release), no bias.
static void test_sgr(void) {
  mouse_state_t st = { .track = MOUSE_TRACK_NORMAL, .enc = MOUSE_ENC_SGR };
  char buf[MOUSE_MAX_SEQ + 1];

  enc_str(&st, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT, 4, 9,
          false, false, false, buf);
  assert(strcmp(buf, "\x1b[<0;5;10M") == 0);

  // Release keeps the button identity but ends with lowercase 'm'.
  enc_str(&st, MOUSE_EVENT_RELEASE, MOUSE_BTN_RIGHT, 4, 9,
          false, false, false, buf);
  assert(strcmp(buf, "\x1b[<2;5;10m") == 0);

  // Large coordinates are allowed in SGR (no 223 clamp).
  enc_str(&st, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT, 299, 399,
          false, false, false, buf);
  assert(strcmp(buf, "\x1b[<0;300;400M") == 0);
}

/// @brief Mode gating: which events each tracking mode reports.
static void test_gating(void) {
  uint8_t out[MOUSE_MAX_SEQ];

  // X10: press only, no release.
  mouse_state_t x10 = { .track = MOUSE_TRACK_X10 };
  assert(mouse_encode(&x10, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT, 0, 0,
                      false, false, false, out) > 0);
  assert(mouse_encode(&x10, MOUSE_EVENT_RELEASE, MOUSE_BTN_LEFT, 0, 0,
                      false, false, false, out) == 0);

  // NORMAL: no motion reporting.
  mouse_state_t normal = { .track = MOUSE_TRACK_NORMAL };
  assert(mouse_encode(&normal, MOUSE_EVENT_MOTION, MOUSE_BTN_LEFT, 0, 0,
                      false, false, false, out) == 0);

  // BUTTON: motion reported only while a button is held.
  mouse_state_t btn = { .track = MOUSE_TRACK_BUTTON };
  assert(mouse_encode(&btn, MOUSE_EVENT_MOTION, MOUSE_BTN_NONE, 0, 0,
                      false, false, false, out) == 0);
  assert(mouse_encode(&btn, MOUSE_EVENT_MOTION, MOUSE_BTN_LEFT, 0, 0,
                      false, false, false, out) > 0);

  // ANY: motion reported even without a button held.
  mouse_state_t any = { .track = MOUSE_TRACK_ANY };
  assert(mouse_encode(&any, MOUSE_EVENT_MOTION, MOUSE_BTN_NONE, 0, 0,
                      false, false, false, out) > 0);
}

/// @brief Motion sets the motion flag (32) plus the held button code.
static void test_motion_flag(void) {
  mouse_state_t st = { .track = MOUSE_TRACK_BUTTON, .enc = MOUSE_ENC_SGR };
  char buf[MOUSE_MAX_SEQ + 1];
  // Left-drag: cb = 32 (motion) + 0 (left) = 32.
  enc_str(&st, MOUSE_EVENT_MOTION, MOUSE_BTN_LEFT, 0, 0,
          false, false, false, buf);
  assert(strcmp(buf, "\x1b[<32;1;1M") == 0);
}

/// @brief Legacy coordinates clamp to the 223 encodable maximum.
static void test_legacy_clamp(void) {
  mouse_state_t st = { .track = MOUSE_TRACK_NORMAL, .enc = MOUSE_ENC_DEFAULT };
  char buf[MOUSE_MAX_SEQ + 1];
  // col=300 -> cx clamps to 223 -> byte 255.
  int n = enc_str(&st, MOUSE_EVENT_PRESS, MOUSE_BTN_LEFT, 300, 300,
                  false, false, false, buf);
  assert(n == 6);
  assert((unsigned char)buf[4] == 255);
  assert((unsigned char)buf[5] == 255);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  printf("mouse tests:\n");

  TEST("set mode");             test_set_mode();             PASS();
  TEST("mode reset semantics"); test_mode_reset_semantics(); PASS();
  TEST("off encodes nothing");  test_off_encodes_nothing();  PASS();
  TEST("legacy press");         test_legacy_press();         PASS();
  TEST("legacy release");       test_legacy_release();       PASS();
  TEST("modifiers");            test_modifiers();            PASS();
  TEST("wheel");                test_wheel();                PASS();
  TEST("sgr encoding");         test_sgr();                  PASS();
  TEST("mode gating");          test_gating();               PASS();
  TEST("motion flag");          test_motion_flag();          PASS();
  TEST("legacy clamp");         test_legacy_clamp();         PASS();

  printf("\n%d / %d tests passed.\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
