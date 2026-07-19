#ifndef MOUSE_H
#define MOUSE_H

// ---------------------------------------------------------------------------
// Mouse tracking → terminal byte sequence converter
//
// Encodes pointer events (button press/release, motion, wheel) into the
// xterm mouse reporting escape sequences that applications enable via the
// DEC private modes (CSI ? 9/1000/1002/1003 h) and select an encoding for
// via CSI ? 1006 h (SGR extended coordinates).
//
// The encoding is pure (no raylib / PTY dependency) so it can be unit
// tested; the polling of raylib pointer state and forwarding to the PTY
// lives in main.c.
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>

/**
 * Maximum length (in bytes) of a single encoded mouse report.  The SGR
 * form "\x1B[<Cb;Cx;CyM" with three-digit fields is the longest.
 */
#define MOUSE_MAX_SEQ 32

/**
 * Number of scrollback lines moved per mouse-wheel notch when the
 * application has not enabled mouse reporting (local scrollback scroll).
 */
#define MOUSE_WHEEL_LINES 3

/// Mouse tracking mode, mirroring the DEC private mode number that
/// enabled it.
typedef enum {
  MOUSE_TRACK_OFF    = 0,     ///< no reporting
  MOUSE_TRACK_X10    = 9,     ///< X10 compatibility: button press only
  MOUSE_TRACK_NORMAL = 1000,  ///< press + release
  MOUSE_TRACK_BUTTON = 1002,  ///< press + release + motion while a button is held
  MOUSE_TRACK_ANY    = 1003,  ///< press + release + all motion
} mouse_track_mode_t;

/// Coordinate/button encoding scheme.
typedef enum {
  MOUSE_ENC_DEFAULT = 0,     ///< legacy X10 encoding (byte = value + 32)
  MOUSE_ENC_SGR     = 1006,  ///< SGR extended encoding (CSI < b;x;y M/m)
} mouse_enc_t;

/// Aggregate mouse reporting state, updated from DECSET/DECRST.
typedef struct {
  mouse_track_mode_t track;
  mouse_enc_t        enc;
} mouse_state_t;

/// Kind of pointer event to report.
typedef enum {
  MOUSE_EVENT_PRESS,
  MOUSE_EVENT_RELEASE,
  MOUSE_EVENT_MOTION,
  MOUSE_EVENT_WHEEL_UP,
  MOUSE_EVENT_WHEEL_DOWN,
} mouse_event_kind_t;

/// Button identifier as reported to the application.
typedef enum {
  MOUSE_BTN_LEFT   = 0,
  MOUSE_BTN_MIDDLE = 1,
  MOUSE_BTN_RIGHT  = 2,
  MOUSE_BTN_NONE   = 3,  ///< no button (legacy release / button-less motion)
} mouse_button_t;

/**
 * Update mouse tracking state from a DEC private mode set (`h`) or
 * reset (`l`).
 *
 * @param st      Mouse state to modify.
 * @param mode    DEC private mode number (e.g. 1000, 1002, 1003, 1006).
 * @param enable  true for DECSET (`h`), false for DECRST (`l`).
 * @return        true if @p mode is a recognised mouse mode.
 */
bool mouse_set_mode(mouse_state_t *st, int mode, bool enable);

/**
 * Encode a pointer event into a terminal report.
 *
 * Events that the current tracking mode does not report (e.g. motion in
 * MOUSE_TRACK_NORMAL, or a release in MOUSE_TRACK_X10) yield 0 bytes.
 *
 * @param st       Current mouse reporting state.
 * @param kind     Event kind.
 * @param button   Button involved (or held button, for motion).
 * @param col      0-based column of the cell under the pointer.
 * @param row      0-based row of the cell under the pointer.
 * @param shift    Shift modifier held.
 * @param alt      Alt/Meta modifier held.
 * @param ctrl     Ctrl modifier held.
 * @param out      Buffer (size MOUSE_MAX_SEQ) to receive the sequence.
 * @return         Number of bytes written to @p out (0 if not reported).
 */
int mouse_encode(const mouse_state_t *st, mouse_event_kind_t kind,
                 mouse_button_t button, int col, int row,
                 bool shift, bool alt, bool ctrl,
                 uint8_t out[MOUSE_MAX_SEQ]);

#endif // MOUSE_H
