/**
 * @file parser.h
 * @brief ANSI escape sequence parser (ECMA-48 / VT100 / xterm).
 *
 * A callback-driven state machine that parses terminal control sequences
 * from a byte stream.  Supports CSI, OSC, DCS, SOS/PM/APC, ESC sequences,
 * C0 controls, and printable text.  Zero-allocation design — all state
 * is stored in the parser_t struct provided by the caller.
 */
#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PARSER_MAX_PARAMS 16
#define PARSER_MAX_INTERMEDIATES 2
#define PARSER_MAX_OSC_STRING 256

// ---------------------------------------------------------------------------
// Callback structure – the host fills this in before feeding data.
// All callbacks are optional (may be NULL).
// ---------------------------------------------------------------------------

typedef struct {
  // Called for printable characters (0x20-0x7E ASCII, 0x80+ UTF-8).
  void (*on_print)(uint8_t ch, void *ctx);

  // Called for C0 control characters (0x00 – 0x1F, except ESC 0x1B).
  void (*on_execute)(char c0, void *ctx);

  // Called when a complete CSI sequence has been parsed.
  // `params` contains the numeric parameters (-1 means omitted/default).
  // `num_params` is the count of valid entries in `params`.
  // `intermediates` contains intermediate bytes (e.g. '?' for DEC private).
  // `num_intermediates` is the count.
  // `final` is the final byte of the sequence (0x40 – 0x7E).
  void (*on_csi)(int params[PARSER_MAX_PARAMS], int num_params,
                 char intermediates[PARSER_MAX_INTERMEDIATES],
                 int num_intermediates, char final, void *ctx);

  // Called when a complete ESC sequence (not CSI) is parsed.
  // `intermediates` and `final` are as above.
  // `num_intermediates` is the count.
  // `final` is the final byte (0x20 – 0x2F intermediates, 0x30 – 0x7E final).
  void (*on_esc)(char intermediates[PARSER_MAX_INTERMEDIATES],
                 int num_intermediates, char final, void *ctx);

  // Called when an OSC (Operating System Command) sequence terminates.
  // `str` points to a NUL-terminated string (the content between ESC ] and ST).
  // `command` is the OSC number (first parameter).
  // NOTE: `str` is only valid during the callback; copy it if needed later.
  void (*on_osc)(int command, const char *str, void *ctx);

  // Called when a DCS (Device Control String) sequence terminates.
  // Not fully implemented yet – provided for future use.
  void (*on_dcs)(int command, const char *str, void *ctx);

  // Called when an SOS/PM/APC string terminates.
  void (*on_string)(const char *str, void *ctx);
} parser_callbacks_t;

// ---------------------------------------------------------------------------
// Parser state (opaque to callers).
// ---------------------------------------------------------------------------

typedef enum {
  PARSER_GROUND,
  PARSER_ESC,
  PARSER_CSI,
  PARSER_CSI_PARAM,
  PARSER_CSI_INTERMEDIATE,
  PARSER_OSC,
  PARSER_DCS,
  PARSER_SOS_PM_APC_STRING,
} parser_state_t;

typedef struct {
  parser_state_t state;
  parser_callbacks_t callbacks;
  void *ctx;

  // Internal accumulators.
  int params[PARSER_MAX_PARAMS];
  int num_params;
  char intermediates[PARSER_MAX_INTERMEDIATES];
  int num_intermediates;
  char osc_string[PARSER_MAX_OSC_STRING];
  int osc_len;
  char dcs_string[PARSER_MAX_OSC_STRING];
  int dcs_len;
  char string_buf[PARSER_MAX_OSC_STRING];
  int string_len;
} parser_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/** Initialise the parser.  callbacks may be NULL (all will be ignored). */
void parser_init(parser_t *p, const parser_callbacks_t *callbacks, void *ctx);

/** Reset the parser to GROUND state (e.g. on error or when discarding input). */
void parser_reset(parser_t *p);

/**
 * Feed one byte of data into the state machine.
 * This may invoke zero or more callbacks.
 */
void parser_advance(parser_t *p, uint8_t b);

/**
 * Feed a buffer of bytes into the state machine.
 * Equivalent to calling parser_advance for each byte.
 */
void parser_feed(parser_t *p, const uint8_t *buf, size_t len);

#endif // PARSER_H
