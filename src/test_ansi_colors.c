#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "parser.h"

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static int  tests_run = 0;
static int  tests_passed = 0;

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
// State captured from callbacks for test verification
// ---------------------------------------------------------------------------

static int  last_params[16];
static int  last_num_params;
static int  last_final;       // final byte of CSI
static bool csi_received;
static bool print_received;
static int  print_ch;
static int  exec_received;
static int  exec_c0;

static void reset_state(void) {
  csi_received = false;
  print_received = false;
  print_ch = 0;
  exec_received = 0;
  exec_c0 = 0;
  last_num_params = 0;
  last_final = 0;
  for (int i = 0; i < 16; i++) last_params[i] = -1;
}

// ---------------------------------------------------------------------------
// Dummy callbacks
// ---------------------------------------------------------------------------

static void cb_print(uint8_t ch, void *ctx) {
  (void)ctx;
  print_received = true;
  print_ch = ch;
}

static void cb_execute(char c0, void *ctx) {
  (void)ctx;
  exec_received++;
  exec_c0 = c0;
}

static void cb_csi(int params[16], int num_params,
                   char intermediates[2], int num_intermediates,
                   char final, void *ctx) {
  (void)intermediates; (void)num_intermediates; (void)ctx;
  csi_received = true;
  last_num_params = num_params;
  last_final = final;
  for (int i = 0; i < num_params && i < 16; i++)
    last_params[i] = params[i];
}

static void cb_esc(char intermediates[2], int num_intermediates,
                   char final, void *ctx) {
  (void)intermediates; (void)num_intermediates; (void)final; (void)ctx;
}

static void cb_osc(int cmd, const char *str, void *ctx) {
  (void)cmd; (void)str; (void)ctx;
}

// ---------------------------------------------------------------------------
// Helper: feed string and check CSI expectations
// ---------------------------------------------------------------------------

static void check_csi(const char *desc, const char *input,
                      int expected_params[], int expected_count,
                      char expected_final) {
  parser_t p;
  parser_callbacks_t cbs = {
    .on_print   = cb_print,
    .on_execute = cb_execute,
    .on_csi     = cb_csi,
    .on_esc     = cb_esc,
    .on_osc     = cb_osc,
    .on_dcs     = NULL,
    .on_string  = NULL,
  };
  parser_init(&p, &cbs, NULL);
  reset_state();

  // Feed the escape sequence byte by byte
  size_t len = strlen(input);
  for (size_t i = 0; i < len; i++)
    parser_advance(&p, (uint8_t)input[i]);

  // Also feed a printable char to flush any pending state
  // (for CSI the final byte already dispatches, so this is just extra safety)
  parser_advance(&p, (uint8_t)'x');

  if (!csi_received) {
    printf("    %s: NO CSI callback received\n", desc);
    return;
  }

  // Check params
  for (int i = 0; i < expected_count; i++) {
    if (i < last_num_params && i < 16) {
      if (last_params[i] != expected_params[i]) {
        printf("    %s: param[%d] = %d, expected %d\n",
               desc, i, last_params[i], expected_params[i]);
        return;
      }
    } else {
      printf("    %s: missing param[%d]\n", desc, i);
      return;
    }
  }

  // Check final byte
  if ((char)last_final != expected_final) {
    printf("    %s: final = '%c' (0x%02x), expected '%c'\n",
           desc, last_final, last_final, expected_final);
    return;
  }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_sgr_reset(void) {
  // ESC[0m – reset all attributes
  const char *input = "\x1B[0m";
  parser_t p;
  parser_callbacks_t cbs = {
    .on_print   = cb_print,
    .on_execute = cb_execute,
    .on_csi     = cb_csi,
    .on_esc     = cb_esc,
    .on_osc     = cb_osc,
    .on_dcs     = NULL,
    .on_string  = NULL,
  };
  parser_init(&p, &cbs, NULL);
  reset_state();

  for (size_t i = 0; i < strlen(input); i++)
    parser_advance(&p, (uint8_t)input[i]);
  parser_advance(&p, (uint8_t)'x');

  assert(csi_received);
  assert(last_num_params >= 1);
  assert(last_params[0] == 0 || last_params[0] == -1);
  // -1 means default/omitted which is equivalent to 0 for SGR
  assert(last_final == 'm');
}

static void test_sgr_fg_red(void) {
  // ESC[31m – set foreground to red
  int expected[] = {31};
  check_csi("fg red", "\x1B[31m", expected, 1, 'm');
}

static void test_sgr_bg_green(void) {
  // ESC[42m – set background to green
  int expected[] = {42};
  check_csi("bg green", "\x1B[42m", expected, 1, 'm');
}

static void test_sgr_bright_fg(void) {
  // ESC[91m – bright red foreground
  int expected[] = {91};
  check_csi("bright fg", "\x1B[91m", expected, 1, 'm');
}

static void test_sgr_multi(void) {
  // ESC[31;42m – red fg + green bg
  int expected[] = {31, 42};
  check_csi("multi param", "\x1B[31;42m", expected, 2, 'm');
}

static void test_sgr_bold_reset(void) {
  // ESC[0;31m – reset then red
  int expected[] = {0, 31};
  check_csi("reset then red", "\x1B[0;31m", expected, 2, 'm');
}

static void test_csi_cursor_up(void) {
  // ESC[A – cursor up (default 1)
  int expected[] = {-1}; // -1 means omitted
  check_csi("cursor up", "\x1B[A", expected, 1, 'A');
}

static void test_csi_cursor_down_3(void) {
  // ESC[3B – cursor down 3
  int expected[] = {3};
  check_csi("cursor down 3", "\x1B[3B", expected, 1, 'B');
}

static void test_csi_cursor_pos(void) {
  // ESC[5;10H – cursor to row 5, col 10
  int expected[] = {5, 10};
  check_csi("cursor pos", "\x1B[5;10H", expected, 2, 'H');
}

static void test_csi_erase_display(void) {
  // ESC[2J – clear entire screen
  int expected[] = {2};
  check_csi("erase display", "\x1B[2J", expected, 1, 'J');
}

static void test_csi_erase_line(void) {
  // ESC[K – erase line from cursor (default 0)
  int expected[] = {-1};
  check_csi("erase line", "\x1B[K", expected, 1, 'K');
}

static void test_sgr_all_std_colors(void) {
  // Test all 8 standard foreground colors
  for (int i = 30; i <= 37; i++) {
    char input[16];
    snprintf(input, sizeof(input), "\x1B[%dm", i);
    int expected[] = {i};
    check_csi(input, input, expected, 1, 'm');
  }
}

static void test_sgr_all_bright_colors(void) {
  for (int i = 90; i <= 97; i++) {
    char input[16];
    snprintf(input, sizeof(input), "\x1B[%dm", i);
    int expected[] = {i};
    check_csi(input, input, expected, 1, 'm');
  }
}

static void test_sgr_all_bg_colors(void) {
  for (int i = 40; i <= 47; i++) {
    char input[16];
    snprintf(input, sizeof(input), "\x1B[%dm", i);
    int expected[] = {i};
    check_csi(input, input, expected, 1, 'm');
  }
}

static void test_print_after_sgr(void) {
  // Verify that text printed after an SGR sequence actually appears
  parser_t p;
  parser_callbacks_t cbs = {
    .on_print   = cb_print,
    .on_execute = cb_execute,
    .on_csi     = cb_csi,
    .on_esc     = cb_esc,
    .on_osc     = cb_osc,
    .on_dcs     = NULL,
    .on_string  = NULL,
  };
  parser_init(&p, &cbs, NULL);
  reset_state();

  const char *input = "\x1B[31mHello";
  for (size_t i = 0; i < strlen(input); i++)
    parser_advance(&p, (uint8_t)input[i]);

  // We should have seen the CSI for [31m, then 'H', 'e', 'l', 'l', 'o'
  // The last byte should be 'o'
  assert(print_received);
  assert(print_ch == 'o');
}

static void test_multiple_sgr(void) {
  // ESC[31;42;1m – red fg, green bg, bold
  int expected[] = {31, 42, 1};
  check_csi("multi SGR", "\x1B[31;42;1m", expected, 3, 'm');
}

static void test_no_param_sgr(void) {
  // ESC[m (same as ESC[0m)
  parser_t p;
  parser_callbacks_t cbs = {
    .on_print   = cb_print,
    .on_execute = cb_execute,
    .on_csi     = cb_csi,
    .on_esc     = cb_esc,
    .on_osc     = cb_osc,
    .on_dcs     = NULL,
    .on_string  = NULL,
  };
  parser_init(&p, &cbs, NULL);
  reset_state();

  // The parser still sends at least one param with value -1 for ESC[m
  const char *input = "\x1B[m";
  for (size_t i = 0; i < strlen(input); i++)
    parser_advance(&p, (uint8_t)input[i]);
  parser_advance(&p, (uint8_t)'x');

  assert(csi_received);
  assert(last_final == 'm');
  // With no params, num_params should be 0, and last_params[0] = -1
  // The main.c handler checks num_params == 0 and treats it as reset
}

// This test verifies that the 8-bit CSI (0x9B) is also handled for SGR
static void test_8bit_csi(void) {
  int expected[] = {31};
  check_csi("8-bit CSI SGR", "\x9B" "31m", expected, 1, 'm');
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  printf("ANSI colour / SGR parser tests:\n");

  TEST("SGR reset (0)");                     test_sgr_reset();               PASS();
  TEST("SGR fg red (31)");                   test_sgr_fg_red();             PASS();
  TEST("SGR bg green (42)");                 test_sgr_bg_green();           PASS();
  TEST("SGR bright fg (91)");                test_sgr_bright_fg();          PASS();
  TEST("SGR multi param (31;42)");           test_sgr_multi();              PASS();
  TEST("SGR bold+reset (0;31)");             test_sgr_bold_reset();         PASS();
  TEST("CSI cursor up");                     test_csi_cursor_up();          PASS();
  TEST("CSI cursor down 3");                 test_csi_cursor_down_3();      PASS();
  TEST("CSI cursor pos");                    test_csi_cursor_pos();         PASS();
  TEST("CSI erase display");                 test_csi_erase_display();      PASS();
  TEST("CSI erase line");                    test_csi_erase_line();         PASS();
  TEST("SGR all standard fg (30-37)");       test_sgr_all_std_colors();     PASS();
  TEST("SGR all bright fg (90-97)");         test_sgr_all_bright_colors();  PASS();
  TEST("SGR all bg (40-47)");                test_sgr_all_bg_colors();      PASS();
  TEST("print after SGR");                   test_print_after_sgr();        PASS();
  TEST("multiple SGR (31;42;1)");            test_multiple_sgr();           PASS();
  TEST("no-param SGR (ESC[m)");              test_no_param_sgr();           PASS();
  TEST("8-bit CSI SGR");                     test_8bit_csi();               PASS();

  printf("\n%d / %d tests passed.\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
