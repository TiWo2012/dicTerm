#include "mouse.h"

#include <stdio.h>

/**
 * @file mouse.c
 * @brief xterm mouse reporting encoder.
 *
 * Translates pointer events into the escape sequences described in the
 * xterm control-sequence documentation ("Mouse Tracking").  Two encodings
 * are supported:
 *
 *   - Legacy (X10): `CSI M Cb Cx Cy` where each byte carries value + 32.
 *     Coordinates are clamped to the 1..223 range the encoding can hold.
 *   - SGR (1006):   `CSI < Cb ; Cx ; Cy M` for press/motion/wheel and
 *     `CSI < Cb ; Cx ; Cy m` for release, with no +32 bias and arbitrary
 *     coordinate magnitude.
 *
 * The low two bits of Cb select the button (0 left, 1 middle, 2 right,
 * 3 = no button / release in the legacy scheme); bit 5 (32) marks motion
 * events and bit 6 (64) marks wheel events.  Modifier bits are 4 (shift),
 * 8 (meta/alt) and 16 (control).
 */

// Modifier bit flags added into the button byte.
enum {
  MOUSE_MOD_SHIFT  = 4,
  MOUSE_MOD_META   = 8,
  MOUSE_MOD_CTRL   = 16,
  MOUSE_FLAG_MOTION = 32,
  MOUSE_FLAG_WHEEL  = 64,
};

bool mouse_set_mode(mouse_state_t *st, int mode, bool enable) {
  if (!st) return false;

  switch (mode) {
  case 9: // X10 compatibility mouse tracking
    if (enable) st->track = MOUSE_TRACK_X10;
    else if (st->track == MOUSE_TRACK_X10) st->track = MOUSE_TRACK_OFF;
    return true;
  case 1000: // normal tracking (press + release)
    if (enable) st->track = MOUSE_TRACK_NORMAL;
    else if (st->track == MOUSE_TRACK_NORMAL) st->track = MOUSE_TRACK_OFF;
    return true;
  case 1002: // button-event tracking (adds motion while a button is down)
    if (enable) st->track = MOUSE_TRACK_BUTTON;
    else if (st->track == MOUSE_TRACK_BUTTON) st->track = MOUSE_TRACK_OFF;
    return true;
  case 1003: // any-event tracking (all motion)
    if (enable) st->track = MOUSE_TRACK_ANY;
    else if (st->track == MOUSE_TRACK_ANY) st->track = MOUSE_TRACK_OFF;
    return true;
  case 1006: // SGR extended coordinate encoding
    st->enc = enable ? MOUSE_ENC_SGR : MOUSE_ENC_DEFAULT;
    return true;
  case 1005: // UTF-8 encoding – not supported, acknowledge to avoid confusion
  case 1015: // urxvt encoding  – not supported, acknowledge
    return true;
  default:
    return false;
  }
}

/// @brief Decide whether @p kind is reported under the current tracking mode.
static bool event_reported(const mouse_state_t *st, mouse_event_kind_t kind,
                           mouse_button_t button) {
  switch (kind) {
  case MOUSE_EVENT_PRESS:
  case MOUSE_EVENT_WHEEL_UP:
  case MOUSE_EVENT_WHEEL_DOWN:
    return true;
  case MOUSE_EVENT_RELEASE:
    // X10 mode reports presses only.
    return st->track != MOUSE_TRACK_X10;
  case MOUSE_EVENT_MOTION:
    if (st->track == MOUSE_TRACK_ANY) return true;
    if (st->track == MOUSE_TRACK_BUTTON) return button != MOUSE_BTN_NONE;
    return false;
  }
  return false;
}

/// @brief Compute the button/flags byte (without the legacy +32 bias).
static int compute_cb(const mouse_state_t *st, mouse_event_kind_t kind,
                      mouse_button_t button, bool shift, bool alt, bool ctrl) {
  int cb;
  switch (kind) {
  case MOUSE_EVENT_WHEEL_UP:
    cb = MOUSE_FLAG_WHEEL + 0;
    break;
  case MOUSE_EVENT_WHEEL_DOWN:
    cb = MOUSE_FLAG_WHEEL + 1;
    break;
  case MOUSE_EVENT_MOTION:
    cb = MOUSE_FLAG_MOTION + (int)button;
    break;
  case MOUSE_EVENT_RELEASE:
    // SGR keeps the button identity (distinguished by the final 'm');
    // the legacy scheme cannot, so it reports button 3.
    cb = (st->enc == MOUSE_ENC_SGR) ? (int)button : (int)MOUSE_BTN_NONE;
    break;
  case MOUSE_EVENT_PRESS:
  default:
    cb = (int)button;
    break;
  }

  if (shift) cb += MOUSE_MOD_SHIFT;
  if (alt)   cb += MOUSE_MOD_META;
  if (ctrl)  cb += MOUSE_MOD_CTRL;
  return cb;
}

int mouse_encode(const mouse_state_t *st, mouse_event_kind_t kind,
                 mouse_button_t button, int col, int row,
                 bool shift, bool alt, bool ctrl,
                 uint8_t out[MOUSE_MAX_SEQ]) {
  if (!st || !out) return 0;
  if (st->track == MOUSE_TRACK_OFF) return 0;
  if (!event_reported(st, kind, button)) return 0;

  if (col < 0) col = 0;
  if (row < 0) row = 0;

  int cb = compute_cb(st, kind, button, shift, alt, ctrl);

  // Coordinates are 1-based in the report.
  int cx = col + 1;
  int cy = row + 1;

  if (st->enc == MOUSE_ENC_SGR) {
    char final = (kind == MOUSE_EVENT_RELEASE) ? 'm' : 'M';
    int n = snprintf((char *)out, MOUSE_MAX_SEQ, "\x1b[<%d;%d;%d%c",
                     cb, cx, cy, final);
    if (n < 0 || n >= MOUSE_MAX_SEQ) return 0;
    return n;
  }

  // Legacy X10 encoding: byte = value + 32, clamped to the encodable range.
  if (cx > 223) cx = 223;
  if (cy > 223) cy = 223;

  int pos = 0;
  out[pos++] = 0x1B;
  out[pos++] = '[';
  out[pos++] = 'M';
  out[pos++] = (uint8_t)(32 + cb);
  out[pos++] = (uint8_t)(32 + cx);
  out[pos++] = (uint8_t)(32 + cy);
  return pos;
}
