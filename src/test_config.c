/**
 * @file test_config.c
 * @brief Unit tests for the configuration parser (config.h / config.c).
 *
 * Verifies default values, INI-style parsing of every section, RGB colour
 * parsing edge cases, and graceful handling of missing / malformed files.
 */

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include "config.h"

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
  printf("  %s ... ", name); \
  tests_run++;          \
} while (0)

#define PASS() do { \
  printf("OK\n");   \
  tests_passed++;   \
} while (0)

// Keep a variable referenced even under NDEBUG (Release), where assert()
// expands to nothing and would otherwise trigger -Wunused-variable.
#define USE(var) (void)(var)

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/// @brief config_load() returns the documented defaults.
static void test_load_defaults(void) {
  dicterm_config_t cfg = config_load();
  assert(cfg.rows == 36);
  assert(cfg.cols == 100);
  assert(cfg.scrollback_capacity == 1000);
  assert(cfg.win_width == 1280);
  assert(cfg.win_height == 800);
  assert(cfg.win_padding == 10);
  assert(cfg.font_size > 10.9f && cfg.font_size < 11.1f);
  assert(cfg.default_fg[0] == 220 && cfg.default_fg[1] == 220 && cfg.default_fg[2] == 220);
  assert(cfg.default_bg[0] == 0 && cfg.default_bg[1] == 0 && cfg.default_bg[2] == 0);
  assert(cfg.font_regular[0] == '\0');
  assert(cfg.font_nerd[0] == '\0');
  assert(cfg.font_symbols[0] == '\0');
  USE(cfg);  // referenced by asserts above (vanishes under NDEBUG)
}

/// @brief Parse a fully-populated (flat) config file.
static void test_parse_full(void) {
  const char *text =
    "rows = 50\n"
    "cols = 120\n"
    "scrollback = 2000\n"
    "font_size = 14.5\n"
    "\n"
    "window_width = 1920\n"
    "window_height = 1080\n"
    "window_padding = 20\n"
    "\n"
    "font_regular = /usr/fonts/regular.ttf\n"
    "font_nerd = /usr/fonts/nerd.ttf\n"
    "font_symbols = /usr/fonts/symbols.ttf\n"
    "\n"
    "foreground = 10,20,30\n"
    "background = 200,100,50\n";

  char path[] = "/tmp/dicterm_config_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  FILE *f = fdopen(fd, "w");
  assert(f != NULL);
  fputs(text, f);
  fclose(f);

  dicterm_config_t cfg;
  bool ok = config_load_from(&cfg, path);
  assert(ok);
  assert(cfg.rows == 50);
  assert(cfg.cols == 120);
  assert(cfg.scrollback_capacity == 2000);
  assert(cfg.font_size > 14.4f && cfg.font_size < 14.6f);
  assert(cfg.win_width == 1920);
  assert(cfg.win_height == 1080);
  assert(cfg.win_padding == 20);
  assert(strcmp(cfg.font_regular, "/usr/fonts/regular.ttf") == 0);
  assert(strcmp(cfg.font_nerd, "/usr/fonts/nerd.ttf") == 0);
  assert(strcmp(cfg.font_symbols, "/usr/fonts/symbols.ttf") == 0);
  assert(cfg.default_fg[0] == 10 && cfg.default_fg[1] == 20 && cfg.default_fg[2] == 30);
  assert(cfg.default_bg[0] == 200 && cfg.default_bg[1] == 100 && cfg.default_bg[2] == 50);
  USE(ok);
  USE(cfg);

  unlink(path);
}

/// @brief Unknown keys are ignored; defaults are retained.
static void test_unknown_keys(void) {
  const char *text =
    "bogus_key = whatever\n"
    "rows = 25\n"
    "extra = 999\n"
    "foreground = 1,2,3\n";

  char path[] = "/tmp/dicterm_config_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  FILE *f = fdopen(fd, "w");
  assert(f != NULL);
  fputs(text, f);
  fclose(f);

  dicterm_config_t cfg;
  bool ok = config_load_from(&cfg, path);
  assert(ok);
  assert(cfg.rows == 25);              // parsed
  assert(cfg.cols == 100);             // default retained
  assert(cfg.default_fg[0] == 1 && cfg.default_fg[2] == 3);
  assert(cfg.default_bg[0] == 0);      // unaffected
  assert(cfg.scrollback_capacity == 1000); // unaffected
  USE(ok);
  USE(cfg);

  unlink(path);
}

/// @brief Comments and blank lines are skipped; whitespace is trimmed.
static void test_comments_and_whitespace(void) {
  const char *text =
    "# this is a comment\n"
    "   ; another comment\n"
    "   rows   =   40   \n"
    "\n"
    "  foreground = 255 , 255 , 255 \n";

  char path[] = "/tmp/dicterm_config_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  FILE *f = fdopen(fd, "w");
  assert(f != NULL);
  fputs(text, f);
  fclose(f);

  dicterm_config_t cfg;
  bool ok = config_load_from(&cfg, path);
  assert(ok);
  assert(cfg.rows == 40);
  assert(cfg.default_fg[0] == 255 && cfg.default_fg[1] == 255 && cfg.default_fg[2] == 255);
  USE(ok);
  USE(cfg);

  unlink(path);
}

/// @brief Non-positive numeric values are rejected and defaults kept.
static void test_invalid_numeric_bounds(void) {
  const char *text =
    "rows = 0\n"
    "cols = -5\n"
    "font_size = -1\n"
    "window_width = 0\n"
    "window_padding = -1\n";

  char path[] = "/tmp/dicterm_config_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  FILE *f = fdopen(fd, "w");
  assert(f != NULL);
  fputs(text, f);
  fclose(f);

  dicterm_config_t cfg;
  bool ok = config_load_from(&cfg, path);
  assert(ok);
  assert(cfg.rows == 36);      // rejected → default
  assert(cfg.cols == 100);     // rejected → default
  assert(cfg.font_size > 10.9f && cfg.font_size < 11.1f);
  assert(cfg.win_width == 1280);
  assert(cfg.win_padding == 10);
  USE(ok);
  USE(cfg);

  unlink(path);
}

/// @brief Malformed RGB keeps the default; out-of-range values rejected.
static void test_invalid_rgb(void) {
  const char *text =
    "foreground = 1,2\n"      // too few
    "background = 300,0,0\n"; // out of range

  char path[] = "/tmp/dicterm_config_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  FILE *f = fdopen(fd, "w");
  assert(f != NULL);
  fputs(text, f);
  fclose(f);

  dicterm_config_t cfg;
  bool ok = config_load_from(&cfg, path);
  assert(ok);
  assert(cfg.default_fg[0] == 220);   // malformed → default
  assert(cfg.default_bg[0] == 0);     // out-of-range → default
  USE(ok);
  USE(cfg);

  unlink(path);
}

/// @brief Missing file returns false and leaves config untouched-ish
///        (still initialised to defaults by the function).
static void test_missing_file(void) {
  dicterm_config_t cfg;
  bool ok = config_load_from(&cfg, "/nonexistent/path/dicterm.cfg");
  assert(!ok);
  // Function still fills defaults so callers can use the struct safely.
  assert(cfg.rows == 36);
  assert(cfg.cols == 100);
  USE(ok);
  USE(cfg);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
  printf("config tests:\n");

  TEST("load defaults");            test_load_defaults();         PASS();
  TEST("parse full");               test_parse_full();            PASS();
  TEST("unknown keys ignored");     test_unknown_keys();          PASS();
  TEST("comments/whitespace");      test_comments_and_whitespace(); PASS();
  TEST("invalid numeric bounds");   test_invalid_numeric_bounds(); PASS();
  TEST("invalid rgb");              test_invalid_rgb();           PASS();
  TEST("missing file");             test_missing_file();          PASS();

  printf("\n%d / %d tests passed.\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
