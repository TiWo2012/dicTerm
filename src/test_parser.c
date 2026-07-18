// ---------------------------------------------------------------------------
// Test suite for the ANSI escape sequence parser (parser.c / parser.h)
//
// Build:  cc -std=c2y -Wall -Wextra -Werror -Iinclude \
//             src/parser.c src/test_parser.c -o test_parser
// Run:    ./test_parser
// ---------------------------------------------------------------------------

#include "parser.h"
#include <stdbool.h>
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

#define ASSERT_STR_EQ(a, b, msg)                                          \
  do {                                                                    \
    if (strcmp((a), (b)) != 0) {                                          \
      printf("\n    ASSERT_STR_EQ(\"%s\" == \"%s\"): %s\n",               \
             (a), (b), msg);                                              \
      printf("    File: %s:%d\n", __FILE__, __LINE__);                    \
      return false;                                                       \
    }                                                                     \
  } while (0)

// ---------------------------------------------------------------------------
// Mock callbacks
// ---------------------------------------------------------------------------

typedef enum {
  EVENT_NONE,
  EVENT_PRINT,
  EVENT_EXECUTE,
  EVENT_CSI,
  EVENT_ESC,
  EVENT_OSC,
  EVENT_DCS,
  EVENT_STRING,
} event_type_t;

typedef struct {
  event_type_t type;
  char ch;                       // print / execute
  int params[PARSER_MAX_PARAMS];
  int num_params;
  char intermediates[PARSER_MAX_INTERMEDIATES];
  int num_intermediates;
  char final_char;               // CSI, ESC final byte
  int osc_command;
  char osc_string[256];
  char dcs_string[256];
  char string_buf[256];
} event_t;

#define MAX_EVENTS 64

typedef struct {
  event_t events[MAX_EVENTS];
  int count;
} mock_ctx_t;

static void mock_reset(mock_ctx_t *m) {
  memset(m, 0, sizeof(*m));
}

static event_t *mock_next(mock_ctx_t *m) {
  if (m->count >= MAX_EVENTS)
    return NULL;
  return &m->events[m->count++];
}

static void on_print(uint8_t ch, void *ctx) {
  mock_ctx_t *m = (mock_ctx_t *)ctx;
  event_t *e = mock_next(m);
  if (!e) return;
  e->type = EVENT_PRINT;
  e->ch = ch;
}

static void on_execute(char c0, void *ctx) {
  mock_ctx_t *m = (mock_ctx_t *)ctx;
  event_t *e = mock_next(m);
  if (!e) return;
  e->type = EVENT_EXECUTE;
  e->ch = c0;
}

static void on_csi(int params[PARSER_MAX_PARAMS], int num_params,
                   char intermediates[PARSER_MAX_INTERMEDIATES],
                   int num_intermediates, char final, void *ctx) {
  mock_ctx_t *m = (mock_ctx_t *)ctx;
  event_t *e = mock_next(m);
  if (!e) return;
  e->type = EVENT_CSI;
  memcpy(e->params, params, sizeof(int) * PARSER_MAX_PARAMS);
  e->num_params = num_params;
  memcpy(e->intermediates, intermediates, PARSER_MAX_INTERMEDIATES);
  e->num_intermediates = num_intermediates;
  e->final_char = final;
}

static void on_esc(char intermediates[PARSER_MAX_INTERMEDIATES],
                   int num_intermediates, char final, void *ctx) {
  mock_ctx_t *m = (mock_ctx_t *)ctx;
  event_t *e = mock_next(m);
  if (!e) return;
  e->type = EVENT_ESC;
  memcpy(e->intermediates, intermediates, PARSER_MAX_INTERMEDIATES);
  e->num_intermediates = num_intermediates;
  e->final_char = final;
}

static void on_osc(int command, const char *str, void *ctx) {
  mock_ctx_t *m = (mock_ctx_t *)ctx;
  event_t *e = mock_next(m);
  if (!e) return;
  e->type = EVENT_OSC;
  e->osc_command = command;
  snprintf(e->osc_string, sizeof(e->osc_string), "%s", str ? str : "");
}

static void on_dcs(int command, const char *str, void *ctx) {
  mock_ctx_t *m = (mock_ctx_t *)ctx;
  event_t *e = mock_next(m);
  if (!e) return;
  e->type = EVENT_DCS;
  e->osc_command = command; // reuse for dcs command
  snprintf(e->dcs_string, sizeof(e->dcs_string), "%s", str ? str : "");
}

static void on_string(const char *str, void *ctx) {
  mock_ctx_t *m = (mock_ctx_t *)ctx;
  event_t *e = mock_next(m);
  if (!e) return;
  e->type = EVENT_STRING;
  snprintf(e->string_buf, sizeof(e->string_buf), "%s", str ? str : "");
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Feed a string literal, using its compile-time length (excluding null).
#define FEED(p, str) parser_feed((p), (const uint8_t *)(str), sizeof(str) - 1)

static parser_callbacks_t mock_cbs = {
    .on_print   = on_print,
    .on_execute = on_execute,
    .on_csi     = on_csi,
    .on_esc     = on_esc,
    .on_osc     = on_osc,
    .on_dcs     = on_dcs,
    .on_string  = on_string,
};

static void init(parser_t *p, mock_ctx_t *m) {
  mock_reset(m);
  parser_init(p, &mock_cbs, m);
}

// Helper: feed raw bytes and check a single event.
#define CHECK_EVENT(e, type)                                              \
  do {                                                                    \
    ASSERT_INT_EQ(m->count, 1, "expected exactly one event");             \
    event_t *e = &m->events[0];                                           \
    ASSERT_INT_EQ(e->type, type, "event type mismatch");                  \
  } while (0)

#define CHECK_NO_EVENT(m)                                                 \
  do {                                                                    \
    ASSERT_INT_EQ(m->count, 0, "expected no events");                     \
  } while (0)

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------

// ---- Ground state ----

static bool test_print_ascii(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  const char *text = "Hello, World!";
  parser_feed(&p, (const uint8_t *)text, strlen(text));

  ASSERT_INT_EQ(m.count, (int)strlen(text), "wrong number of print events");
  for (size_t i = 0; i < strlen(text); i++) {
    ASSERT_INT_EQ(m.events[i].type, EVENT_PRINT, "expected print");
    ASSERT_INT_EQ(m.events[i].ch, text[i], "wrong char");
  }
  return true;
}

static bool test_c0_execute(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // Feed various C0 controls: BEL, BS, HT, LF, VT, FF, CR
  uint8_t c0[] = {0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D};
  parser_feed(&p, c0, sizeof(c0));

  ASSERT_INT_EQ(m.count, 7, "expected 7 execute events");
  ASSERT_INT_EQ(m.events[0].type, EVENT_EXECUTE, "BEL");
  ASSERT_INT_EQ(m.events[0].ch, 0x07, "BEL char");
  ASSERT_INT_EQ(m.events[1].type, EVENT_EXECUTE, "BS");
  ASSERT_INT_EQ(m.events[1].ch, 0x08, "BS char");
  ASSERT_INT_EQ(m.events[2].type, EVENT_EXECUTE, "HT");
  ASSERT_INT_EQ(m.events[2].ch, 0x09, "HT char");
  ASSERT_INT_EQ(m.events[3].type, EVENT_EXECUTE, "LF");
  ASSERT_INT_EQ(m.events[3].ch, 0x0A, "LF char");
  ASSERT_INT_EQ(m.events[4].type, EVENT_EXECUTE, "VT");
  ASSERT_INT_EQ(m.events[4].ch, 0x0B, "VT char");
  ASSERT_INT_EQ(m.events[5].type, EVENT_EXECUTE, "FF");
  ASSERT_INT_EQ(m.events[5].ch, 0x0C, "FF char");
  ASSERT_INT_EQ(m.events[6].type, EVENT_EXECUTE, "CR");
  ASSERT_INT_EQ(m.events[6].ch, 0x0D, "CR char");
  return true;
}

static bool test_can_sub_in_ground(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // CAN (0x18) and SUB (0x1A) should call on_execute and stay in ground
  uint8_t buf[] = {'A', 0x18, 'B', 0x1A, 'C'};
  parser_feed(&p, buf, sizeof(buf));

  ASSERT_INT_EQ(m.count, 5, "expected 5 events (3 print + 2 execute)");
  ASSERT_INT_EQ(m.events[0].type, EVENT_PRINT, "A");
  ASSERT_INT_EQ(m.events[1].type, EVENT_EXECUTE, "CAN");
  ASSERT_INT_EQ(m.events[1].ch, 0x18, "CAN");
  ASSERT_INT_EQ(m.events[2].type, EVENT_PRINT, "B");
  ASSERT_INT_EQ(m.events[3].type, EVENT_EXECUTE, "SUB");
  ASSERT_INT_EQ(m.events[3].ch, 0x1A, "SUB");
  ASSERT_INT_EQ(m.events[4].type, EVENT_PRINT, "C");
  return true;
}

static bool test_del_ignored(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  uint8_t buf[] = {'A', 0x7F, 'B'};
  parser_feed(&p, buf, sizeof(buf));

  ASSERT_INT_EQ(m.count, 2, "DEL should be ignored");
  ASSERT_INT_EQ(m.events[0].ch, 'A', "A");
  ASSERT_INT_EQ(m.events[1].ch, 'B', "B");
  return true;
}

// ---- ESC state ----

static bool test_esc_simple(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC 7 (DECSC), ESC 8 (DECRC), ESC c (RIS), ESC D (IND), ESC M (RI)
  FEED(&p, "a\x1b"
            "7b\x1b"
            "8c\x1b"
            "cd\x1b"
            "De\x1b"
            "Mf");

  // Expected: print a, esc(7), print b, esc(8), print c, esc(c),
  //           print d, esc(D), print e, esc(M), print f
  ASSERT_INT_EQ(m.count, 11, "expected 11 events");

  ASSERT_INT_EQ(m.events[0].type, EVENT_PRINT, "a");
  ASSERT_INT_EQ(m.events[1].type, EVENT_ESC, "ESC 7");
  ASSERT_INT_EQ(m.events[1].final_char, '7', "ESC 7 final");
  ASSERT_INT_EQ(m.events[2].type, EVENT_PRINT, "b");
  ASSERT_INT_EQ(m.events[3].type, EVENT_ESC, "ESC 8");
  ASSERT_INT_EQ(m.events[3].final_char, '8', "ESC 8 final");
  ASSERT_INT_EQ(m.events[4].type, EVENT_PRINT, "c");
  ASSERT_INT_EQ(m.events[5].type, EVENT_ESC, "ESC c");
  ASSERT_INT_EQ(m.events[5].final_char, 'c', "ESC c final");
  ASSERT_INT_EQ(m.events[6].type, EVENT_PRINT, "d");
  ASSERT_INT_EQ(m.events[7].type, EVENT_ESC, "ESC D");
  ASSERT_INT_EQ(m.events[7].final_char, 'D', "ESC D final");
  ASSERT_INT_EQ(m.events[8].type, EVENT_PRINT, "e");
  ASSERT_INT_EQ(m.events[9].type, EVENT_ESC, "ESC M");
  ASSERT_INT_EQ(m.events[9].final_char, 'M', "ESC M final");
  ASSERT_INT_EQ(m.events[10].type, EVENT_PRINT, "f");
  return true;
}

static bool test_esc_escape_resets(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // Two ESC in a row: second ESC should reset state (no on_esc dispatched)
  parser_feed(&p, (const uint8_t *)"\x1b\x1b"
                                    "7",
              3);

  // Should be just ESC 7 (DECSC)
  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_ESC, "ESC 7");
  ASSERT_INT_EQ(m.events[0].final_char, '7', "7");
  return true;
}

static bool test_esc_intermediates(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC (final byte after space intermediate)
  parser_feed(&p, (const uint8_t *)"\x1b F", 3);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_ESC, "ESC (F");
  ASSERT_INT_EQ(m.events[0].final_char, 'F', "F");
  ASSERT_INT_EQ(m.events[0].num_intermediates, 1, "1 intermediate");
  ASSERT_INT_EQ(m.events[0].intermediates[0], ' ', "intermediate space");
  return true;
}

static bool test_esc_can_sub_abort(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC then CAN – should abort, return to ground
  parser_feed(&p, (const uint8_t *)"\x1b\x18", 2);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event (execute for CAN)");
  ASSERT_INT_EQ(m.events[0].type, EVENT_EXECUTE, "CAN");
  ASSERT_INT_EQ(m.events[0].ch, 0x18, "CAN");
  return true;
}

// ---- CSI state ----

static bool test_csi_simple(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC [ A  -> CUU (no params -> default)
  parser_feed(&p, (const uint8_t *)"\x1b[A", 3);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[0].final_char, 'A', "CUU");
  ASSERT_INT_EQ(m.events[0].num_params, 1, "1 param (default: -1)");
  ASSERT_INT_EQ(m.events[0].params[0], -1, "default param == -1");
  ASSERT_INT_EQ(m.events[0].num_intermediates, 0, "no intermediates");
  return true;
}

static bool test_csi_single_param(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC [ 3 B  -> CUD 3
  parser_feed(&p, (const uint8_t *)"\x1b[3B", 4);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[0].final_char, 'B', "CUD");
  ASSERT_INT_EQ(m.events[0].num_params, 1, "1 param");
  ASSERT_INT_EQ(m.events[0].params[0], 3, "param == 3");
  return true;
}

static bool test_csi_multi_param(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC [ 3 ; 5 H  -> CUP row=3 col=5
  parser_feed(&p, (const uint8_t *)"\x1b[3;5H", 6);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[0].final_char, 'H', "CUP");
  ASSERT_INT_EQ(m.events[0].num_params, 2, "2 params");
  ASSERT_INT_EQ(m.events[0].params[0], 3, "row=3");
  ASSERT_INT_EQ(m.events[0].params[1], 5, "col=5");
  return true;
}

static bool test_csi_omitted_params(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC [ ; ; H  -> all omitted params = -1
  parser_feed(&p, (const uint8_t *)"\x1b[;;H", 5);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[0].final_char, 'H', "CUP");
  // Three params: first omitted (-1), second omitted (-1), third implicit (-1)
  ASSERT_INT_EQ(m.events[0].num_params, 3, "3 params");
  ASSERT_INT_EQ(m.events[0].params[0], -1, "param0 omitted");
  ASSERT_INT_EQ(m.events[0].params[1], -1, "param1 omitted");
  ASSERT_INT_EQ(m.events[0].params[2], -1, "param2 omitted (implicit)");
  return true;
}

static bool test_csi_multi_digit_param(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC [ 123 A  -> CUU 123
  parser_feed(&p, (const uint8_t *)"\x1b[123A", 6);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[0].final_char, 'A', "CUU");
  ASSERT_INT_EQ(m.events[0].params[0], 123, "param == 123");
  return true;
}

static bool test_csi_colon_subparam(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC [ 38 : 5 : 10 m  -> SGR sub-parameters using ':'
  parser_feed(&p, (const uint8_t *)"\x1b[38:5:10m", 10);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[0].final_char, 'm', "SGR");
  ASSERT_INT_EQ(m.events[0].num_params, 3, "3 params");
  ASSERT_INT_EQ(m.events[0].params[0], 38, "extended fg");
  ASSERT_INT_EQ(m.events[0].params[1], 5, "256-colour mode");
  ASSERT_INT_EQ(m.events[0].params[2], 10, "colour index 10");
  return true;
}

static bool test_csi_private_marker(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC [ ? 1 h  -> DECSET
  parser_feed(&p, (const uint8_t *)"\x1b[?1h", 5);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[0].final_char, 'h', "DECSET");
  ASSERT_INT_EQ(m.events[0].num_intermediates, 1, "1 intermediate");
  ASSERT_INT_EQ(m.events[0].intermediates[0], '?', "private marker '?'");
  ASSERT_INT_EQ(m.events[0].params[0], 1, "param 1");
  return true;
}

static bool test_csi_intermediate(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC [ with space intermediate + q final
  FEED(&p, "\x1b[ q");

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[0].final_char, 'q', "final q");
  ASSERT_INT_EQ(m.events[0].num_intermediates, 1, "1 intermediate");
  ASSERT_INT_EQ(m.events[0].intermediates[0], ' ', "space intermediate");
  return true;
}

static bool test_csi_sgr(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC [ 1 ; 31 m  -> SGR bold red
  parser_feed(&p, (const uint8_t *)"\x1b[1;31m", 7);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[0].final_char, 'm', "SGR");
  ASSERT_INT_EQ(m.events[0].num_params, 2, "2 params");
  ASSERT_INT_EQ(m.events[0].params[0], 1, "bold");
  ASSERT_INT_EQ(m.events[0].params[1], 31, "red");
  return true;
}

static bool test_csi_can_sub_abort(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC [ 3 ; CAN -> should abort CSI, CAN execute, then 'A' should print
  FEED(&p, "\x1b[3;\x18"
            "A");

  ASSERT_INT_EQ(m.count, 2, "expected 2 events");
  ASSERT_INT_EQ(m.events[0].type, EVENT_EXECUTE, "CAN");
  ASSERT_INT_EQ(m.events[0].ch, 0x18, "CAN");
  ASSERT_INT_EQ(m.events[1].type, EVENT_PRINT, "A");
  return true;
}

static bool test_csi_esc_restart(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC [ 3 ; ESC A -> second ESC resets, then 'A' should be
  // an ESC sequence with final 'A', not CSI
  FEED(&p, "\x1b[3;\x1b"
            "A");

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_ESC, "ESC A");
  ASSERT_INT_EQ(m.events[0].final_char, 'A', "final A");
  return true;
}

static bool test_csi_max_params(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // Construct a CSI with many params: ESC [ 1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17;18;19;20 H
  uint8_t seq[64];
  int len = 2; // indices 0 and 1 are \x1b [
  seq[0] = 0x1B;
  seq[1] = '[';
  for (int i = 1; i <= 20; i++) {
    if (i > 1)
      seq[len++] = ';';
    if (i >= 10)
      seq[len++] = '0' + (i / 10);
    seq[len++] = '0' + (i % 10);
  }
  seq[len++] = 'H';
  parser_feed(&p, seq, (size_t)len);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI");
  // Should have max params = PARSER_MAX_PARAMS (16)
  ASSERT_INT_EQ(m.events[0].num_params, PARSER_MAX_PARAMS,
                "max params capped at PARSER_MAX_PARAMS");
  ASSERT_INT_EQ(m.events[0].params[0], 1, "param[0]=1");
  ASSERT_INT_EQ(m.events[0].params[14], 15, "param[14]=15");
  ASSERT_INT_EQ(m.events[0].params[15], 16, "param[15]=16 (last valid)");
  return true;
}

// ---- OSC state ----

static bool test_osc_bel_terminated(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC ] 0 ; My Title BEL
  parser_feed(&p, (const uint8_t *)"\x1b]0;My Title\x07", 13);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_OSC, "OSC");
  ASSERT_INT_EQ(m.events[0].osc_command, 0, "command 0");
  ASSERT_STR_EQ(m.events[0].osc_string, "My Title", "title string");
  return true;
}

static bool test_osc_st_terminated(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC ] 1 ; foo ESC <ST>
  parser_feed(&p, (const uint8_t *)"\x1b]1;foo\x1b\\", 9);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_OSC, "OSC");
  ASSERT_INT_EQ(m.events[0].osc_command, 1, "command 1");
  ASSERT_STR_EQ(m.events[0].osc_string, "foo", "string foo");
  return true;
}

static bool test_osc_8bit_st_terminated(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC ] 2 ; bar 0x9C (8-bit ST)
  parser_feed(&p, (const uint8_t *)"\x1b]2;bar\x9c", 8);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_OSC, "OSC");
  ASSERT_INT_EQ(m.events[0].osc_command, 2, "command 2");
  ASSERT_STR_EQ(m.events[0].osc_string, "bar", "string bar");
  return true;
}

static bool test_osc_multi_digit_command(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC ] 12 ; hello BEL
  FEED(&p, "\x1b]12;hello\x07");

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_OSC, "OSC");
  ASSERT_INT_EQ(m.events[0].osc_command, 12, "command 12");
  ASSERT_STR_EQ(m.events[0].osc_string, "hello", "string hello");
  return true;
}

static bool test_osc_no_semicolon(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC ] 123 BEL (no semicolon, no string)
  parser_feed(&p, (const uint8_t *)"\x1b]123\x07", 6);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_OSC, "OSC");
  ASSERT_INT_EQ(m.events[0].osc_command, 123, "command 123");
  ASSERT_STR_EQ(m.events[0].osc_string, "", "empty string");
  return true;
}

static bool test_osc_no_command(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC ] ; text BEL (no command number)
  FEED(&p, "\x1b];text\x07");

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_OSC, "OSC");
  ASSERT_INT_EQ(m.events[0].osc_command, -1, "command -1 (not given)");
  ASSERT_STR_EQ(m.events[0].osc_string, ";text", "string includes semicolon");
  return true;
}

static bool test_osc_abort_can(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC ] 0 ; foo CAN (should abort, no OSC dispatch)
  FEED(&p, "\x1b]0;foo\x18");

  // CAN should trigger on_execute, but no OSC dispatch
  ASSERT_INT_EQ(m.count, 1, "expected 1 event (CAN execute)");
  ASSERT_INT_EQ(m.events[0].type, EVENT_EXECUTE, "CAN");
  ASSERT_INT_EQ(m.events[0].ch, 0x18, "CAN");
  return true;
}

// ---- DCS state ----

static bool test_dcs_st_terminated(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC P 1 ; data terminated by ST (DCS)
  FEED(&p, "\x1bP1;data\x1b\\");

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_DCS, "DCS");
  ASSERT_STR_EQ(m.events[0].dcs_string, "1;data", "DCS string");
  return true;
}

static bool test_dcs_8bit_st(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC P hello 0x9C (8-bit ST)
  parser_feed(&p, (const uint8_t *)"\x1bPhello\x9c", 8);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_DCS, "DCS");
  ASSERT_STR_EQ(m.events[0].dcs_string, "hello", "DCS string");
  return true;
}

static bool test_dcs_bel_terminated(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC P data BEL
  parser_feed(&p, (const uint8_t *)"\x1bPdata\x07", 7);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_DCS, "DCS");
  ASSERT_STR_EQ(m.events[0].dcs_string, "data", "DCS string");
  return true;
}

// ---- SOS/PM/APC state ----

static bool test_sos_st_terminated(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC X hello ESC ST (SOS)
  parser_feed(&p, (const uint8_t *)"\x1bXhello\x1b\\", 9);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_STRING, "SOS string");
  ASSERT_STR_EQ(m.events[0].string_buf, "hello", "SOS content");
  return true;
}

static bool test_pm_st_terminated(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC ^ hello ESC ST (PM)
  parser_feed(&p, (const uint8_t *)"\x1b^hello\x1b\\", 9);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_STRING, "PM string");
  ASSERT_STR_EQ(m.events[0].string_buf, "hello", "PM content");
  return true;
}

static bool test_apc_st_terminated(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC _ hello ESC ST (APC)
  parser_feed(&p, (const uint8_t *)"\x1b_hello\x1b\\", 9);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_STRING, "APC string");
  ASSERT_STR_EQ(m.events[0].string_buf, "hello", "APC content");
  return true;
}

// ---- 8-bit control codes ----

static bool test_8bit_csi(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // 0x9B 3 A  (8-bit CSI with param 3, final A)
  parser_feed(&p, (const uint8_t *)"\x9b"
                                    "3A",
              3);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI from 8-bit");
  ASSERT_INT_EQ(m.events[0].final_char, 'A', "CUU");
  ASSERT_INT_EQ(m.events[0].params[0], 3, "param 3");
  return true;
}

static bool test_8bit_osc(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // 0x9D 0 ; text BEL (8-bit OSC)
  parser_feed(&p, (const uint8_t *)"\x9d"
                                    "0;text\x07",
              8);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_OSC, "OSC from 8-bit");
  ASSERT_INT_EQ(m.events[0].osc_command, 0, "command 0");
  ASSERT_STR_EQ(m.events[0].osc_string, "text", "OSC string");
  return true;
}

static bool test_8bit_dcs(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // 0x90 data 0x9C (8-bit DCS with 8-bit ST)
  parser_feed(&p, (const uint8_t *)"\x90"
                                    "data\x9c",
              6);

  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_DCS, "DCS from 8-bit");
  ASSERT_STR_EQ(m.events[0].dcs_string, "data", "DCS string");
  return true;
}

static bool test_8bit_sos_pm_apc(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // 0x98 hello 0x9C (8-bit SOS with 8-bit ST)
  // 0x9E world 0x9C (8-bit PM)
  // 0x9F ! 0x9C    (8-bit APC)
  FEED(&p, "\x98"
           "hello\x9c"
           "\x9e"
           "world\x9c"
           "\x9f!\x9c");

  ASSERT_INT_EQ(m.count, 3, "expected 3 string events");

  ASSERT_INT_EQ(m.events[0].type, EVENT_STRING, "SOS");
  ASSERT_STR_EQ(m.events[0].string_buf, "hello", "SOS content");

  ASSERT_INT_EQ(m.events[1].type, EVENT_STRING, "PM");
  ASSERT_STR_EQ(m.events[1].string_buf, "world", "PM content");

  ASSERT_INT_EQ(m.events[2].type, EVENT_STRING, "APC");
  ASSERT_STR_EQ(m.events[2].string_buf, "!", "APC content");
  return true;
}

// ---- Error recovery ----

static bool test_invalid_byte_resets(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // 0x1B followed by an invalid byte (e.g. 0x03 is execute, not invalid)
  // Actually in ESC state, 0x00-0x17 is execute. Let's use a byte that's
  // below 0x20 but not 0x1B — it'll call execute and stay in ESC.
  // For truly "unexpected", use 0x80+ in ESC state (fallback to ground).
  FEED(&p, "\x1b\x80hello");

  // \x1b -> ESC state; \x80 (0x80) not handled by esc_state -> falls to
  // default -> if b < 0x30, resets to ground without callback.
  // Then "hello" should print.
  ASSERT_INT_EQ(m.count, 5, "expected 5 print events (hello)");
  for (int i = 0; i < 5; i++)
    ASSERT_INT_EQ(m.events[i].type, EVENT_PRINT, "print");
  return true;
}

static bool test_reset_api(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // Start a CSI sequence then reset
  parser_advance(&p, 0x1B);
  parser_advance(&p, '[');
  parser_advance(&p, '3');
  parser_reset(&p);

  // After reset, 'A' should print normally (not interpreted as CSI final)
  parser_advance(&p, 'A');

  ASSERT_INT_EQ(m.count, 1, "expected 1 event (print A)");
  ASSERT_INT_EQ(m.events[0].type, EVENT_PRINT, "A");
  ASSERT_INT_EQ(m.events[0].ch, 'A', "A");
  return true;
}

// ---- Buffer overflow ----

static bool test_osc_buffer_overflow(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // Feed an OSC string longer than PARSER_MAX_OSC_STRING
  // PARSER_MAX_OSC_STRING = 256, so feed 260 bytes
  uint8_t buf[270];
  buf[0] = 0x1B;
  buf[1] = ']';
  buf[2] = '0';
  buf[3] = ';';
  memset(buf + 4, 'x', 260);
  buf[264] = 0x07; // BEL terminator
  parser_feed(&p, buf, 265);

  // Should get an OSC callback with truncated string (max 255 chars)
  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_OSC, "OSC");
  ASSERT_INT_EQ(m.events[0].osc_command, 0, "command 0");
  // The string should be truncated: 255 bytes total stored, minus "0;" prefix
  ASSERT_INT_EQ((int)strlen(m.events[0].osc_string), 253,
                "string truncated to 253 (255 - '0;' prefix)");
  return true;
}

static bool test_osc_st_with_full_buffer(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // Fill osc buffer to near-max, then ST should still terminate
  // Feed ESC ] 0 ; <254 x's> then ESC ST
  uint8_t buf[260 + 4];
  int pos = 0;
  buf[pos++] = 0x1B;
  buf[pos++] = ']';
  buf[pos++] = '0';
  buf[pos++] = ';';
  memset(buf + pos, 'x', 254);
  pos += 254;
  buf[pos++] = 0x1B; // ESC (would overflow buffer, so it's dropped)
  buf[pos++] = '\\'; // Backslash — should trigger ST detection

  parser_feed(&p, buf, (size_t)pos);

  // Should get an OSC callback despite the overflow
  ASSERT_INT_EQ(m.count, 1, "expected 1 event");
  ASSERT_INT_EQ(m.events[0].type, EVENT_OSC, "OSC");
  ASSERT_INT_EQ(m.events[0].osc_command, 0, "command 0");
  return true;
}

// ---- Real-world integration scenarios ----

static bool test_prompt_sequence(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // Simulate what a shell might output: "user@host:~$ " with colors
  // \x1b[1;32muser\x1b[0m@host:\x1b[1;34m~\x1b[0m$
  const uint8_t seq[] = "\x1b[1;32muser\x1b[0m@host:\x1b[1;34m~\x1b[0m$ ";
  parser_feed(&p, seq, sizeof(seq) - 1);

  // Events: CSI(1;32m) + print(u) + print(s) + print(e) + print(r)
  //         + CSI(0m) + print(@) + print(h) + print(o) + print(s) + print(t) + print(:)
  //         + CSI(1;34m) + print(~)
  //         + CSI(0m) + print($) + print( )
  // = 4 CSI + 13 print = 17 events

  ASSERT_INT_EQ(m.count, 17, "expected 17 events from prompt");

  // Check CSI events
  int csi_idx = 0;
  for (int i = 0; i < m.count; i++) {
    if (m.events[i].type == EVENT_CSI) {
      switch (csi_idx) {
      case 0:
        ASSERT_INT_EQ(m.events[i].final_char, 'm', "SGR");
        ASSERT_INT_EQ(m.events[i].params[0], 1, "bold");
        ASSERT_INT_EQ(m.events[i].params[1], 32, "green");
        break;
      case 1:
        ASSERT_INT_EQ(m.events[i].final_char, 'm', "SGR");
        ASSERT_INT_EQ(m.events[i].params[0], 0, "reset");
        break;
      case 2:
        ASSERT_INT_EQ(m.events[i].final_char, 'm', "SGR");
        ASSERT_INT_EQ(m.events[i].params[0], 1, "bold");
        ASSERT_INT_EQ(m.events[i].params[1], 34, "blue");
        break;
      case 3:
        ASSERT_INT_EQ(m.events[i].final_char, 'm', "SGR");
        ASSERT_INT_EQ(m.events[i].params[0], 0, "reset");
        break;
      }
      csi_idx++;
    }
  }
  ASSERT_INT_EQ(csi_idx, 4, "4 CSI sequences");
  return true;
}

static bool test_cursor_movement_sequence(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // Simulate cursor movement: ESC[5A (up 5), ESC[3B (down 3),
  // ESC[10C (forward 10), ESC[2D (back 2), ESC[1;1H (home)
  const uint8_t seq[] =
      "\x1b[5A\x1b[3B\x1b[10C\x1b[2D\x1b[1;1H";
  parser_feed(&p, seq, sizeof(seq) - 1);

  ASSERT_INT_EQ(m.count, 5, "expected 5 CSI events");

  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[0].final_char, 'A', "CUU");
  ASSERT_INT_EQ(m.events[0].params[0], 5, "up 5");

  ASSERT_INT_EQ(m.events[1].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[1].final_char, 'B', "CUD");
  ASSERT_INT_EQ(m.events[1].params[0], 3, "down 3");

  ASSERT_INT_EQ(m.events[2].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[2].final_char, 'C', "CUF");
  ASSERT_INT_EQ(m.events[2].params[0], 10, "forward 10");

  ASSERT_INT_EQ(m.events[3].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[3].final_char, 'D', "CUB");
  ASSERT_INT_EQ(m.events[3].params[0], 2, "back 2");

  ASSERT_INT_EQ(m.events[4].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[4].final_char, 'H', "CUP");
  ASSERT_INT_EQ(m.events[4].params[0], 1, "row 1");
  ASSERT_INT_EQ(m.events[4].params[1], 1, "col 1");
  return true;
}

static bool test_erase_sequences(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // Test ED (erase in display) and EL (erase in line)
  const uint8_t seq[] =
      "\x1b[2J"     // ED 2 (erase entire screen)
      "\x1b[0K"     // EL 0 (erase to end of line)
      "\x1b[1K"     // EL 1 (erase to start of line)
      "\x1b[K"      // EL default (same as 0)
      "\x1b[J";     // ED default (same as 0)
  parser_feed(&p, seq, sizeof(seq) - 1);

  ASSERT_INT_EQ(m.count, 5, "expected 5 CSI events");

  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[0].final_char, 'J', "ED");
  ASSERT_INT_EQ(m.events[0].params[0], 2, "ED 2");

  ASSERT_INT_EQ(m.events[1].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[1].final_char, 'K', "EL");
  ASSERT_INT_EQ(m.events[1].params[0], 0, "EL 0");

  ASSERT_INT_EQ(m.events[2].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[2].final_char, 'K', "EL");
  ASSERT_INT_EQ(m.events[2].params[0], 1, "EL 1");

  ASSERT_INT_EQ(m.events[3].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[3].final_char, 'K', "EL");
  ASSERT_INT_EQ(m.events[3].params[0], -1, "EL default");

  ASSERT_INT_EQ(m.events[4].type, EVENT_CSI, "CSI");
  ASSERT_INT_EQ(m.events[4].final_char, 'J', "ED");
  ASSERT_INT_EQ(m.events[4].params[0], -1, "ED default");
  return true;
}

static bool test_save_restore_cursor(void) {
  parser_t p;
  mock_ctx_t m;
  init(&p, &m);

  // ESC[s (save cursor via CSI), ESC[u (restore via CSI)
  // ESC 7 (save via ESC), ESC 8 (restore via ESC)
  const uint8_t seq[] = "\x1b[s\x1b[u\x1b"
                        "7\x1b"
                        "8";
  parser_feed(&p, seq, sizeof(seq) - 1);

  ASSERT_INT_EQ(m.count, 4, "expected 4 events");

  ASSERT_INT_EQ(m.events[0].type, EVENT_CSI, "CSI s");
  ASSERT_INT_EQ(m.events[0].final_char, 's', "save cursor");

  ASSERT_INT_EQ(m.events[1].type, EVENT_CSI, "CSI u");
  ASSERT_INT_EQ(m.events[1].final_char, 'u', "restore cursor");

  ASSERT_INT_EQ(m.events[2].type, EVENT_ESC, "ESC 7");
  ASSERT_INT_EQ(m.events[2].final_char, '7', "DECSC");

  ASSERT_INT_EQ(m.events[3].type, EVENT_ESC, "ESC 8");
  ASSERT_INT_EQ(m.events[3].final_char, '8', "DECRC");
  return true;
}

// ---- Callback can be NULL ----

static bool test_null_callbacks(void) {
  parser_t p;

  // Init with NULL callbacks - should not crash
  parser_init(&p, NULL, NULL);

  // Feed various sequences - should not crash
  FEED(&p, "hello\x1b[3A\x1b]0;x\x07\x1bPdata\x1b\\");

  // Ensure we're back in ground state
  ASSERT_INT_EQ(p.state, PARSER_GROUND, "state should be GROUND");
  return true;
}

// ---- Main ----

#define RUN_TESTS                                                          \
  do {                                                                     \
    TEST(print_ascii);                                                     \
    TEST(c0_execute);                                                      \
    TEST(can_sub_in_ground);                                               \
    TEST(del_ignored);                                                     \
    TEST(esc_simple);                                                      \
    TEST(esc_escape_resets);                                               \
    TEST(esc_intermediates);                                               \
    TEST(esc_can_sub_abort);                                               \
    TEST(csi_simple);                                                      \
    TEST(csi_single_param);                                                \
    TEST(csi_multi_param);                                                 \
    TEST(csi_omitted_params);                                              \
    TEST(csi_multi_digit_param);                                              \
    TEST(csi_colon_subparam);                                                 \
    TEST(csi_private_marker);                                                 \
    TEST(csi_intermediate);                                                \
    TEST(csi_sgr);                                                         \
    TEST(csi_can_sub_abort);                                               \
    TEST(csi_esc_restart);                                                 \
    TEST(csi_max_params);                                                  \
    TEST(osc_bel_terminated);                                              \
    TEST(osc_st_terminated);                                               \
    TEST(osc_8bit_st_terminated);                                          \
    TEST(osc_multi_digit_command);                                         \
    TEST(osc_no_semicolon);                                                \
    TEST(osc_no_command);                                                  \
    TEST(osc_abort_can);                                                   \
    TEST(dcs_st_terminated);                                               \
    TEST(dcs_8bit_st);                                                     \
    TEST(dcs_bel_terminated);                                              \
    TEST(sos_st_terminated);                                               \
    TEST(pm_st_terminated);                                                \
    TEST(apc_st_terminated);                                               \
    TEST(8bit_csi);                                                   \
    TEST(8bit_osc);                                                   \
    TEST(8bit_dcs);                                                   \
    TEST(8bit_sos_pm_apc);                                            \
    TEST(invalid_byte_resets);                                             \
    TEST(reset_api);                                                       \
    TEST(osc_buffer_overflow);                                             \
    TEST(osc_st_with_full_buffer);                                         \
    TEST(prompt_sequence);                                                 \
    TEST(cursor_movement_sequence);                                        \
    TEST(erase_sequences);                                                 \
    TEST(save_restore_cursor);                                             \
    TEST(null_callbacks);                                                  \
  } while (0)

int main(void) {
  printf("parser test suite\n");
  printf("=================\n\n");

  RUN_TESTS;

  printf("\n");
  printf("=================\n");
  printf("results: %d / %d passed", tests_passed, tests_run);
  if (tests_failed > 0)
    printf(", %d FAILED", tests_failed);
  printf("\n");

  return tests_failed > 0 ? 1 : 0;
}
