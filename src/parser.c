#include "parser.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Reset parameter / intermediate accumulators for a new sequence.
// ---------------------------------------------------------------------------
static void reset_accumulators(parser_t *p) {
  p->num_params = 0;
  for (int i = 0; i < PARSER_MAX_PARAMS; i++)
    p->params[i] = -1;
  p->num_intermediates = 0;
  memset(p->intermediates, 0, PARSER_MAX_INTERMEDIATES);
}

// ---------------------------------------------------------------------------
// Start a new parameter (called when ';' is encountered in CSI_PARAM).
// ---------------------------------------------------------------------------
static void next_param(parser_t *p) {
  if (p->num_params < PARSER_MAX_PARAMS - 1) {
    p->num_params++;
  } else if (p->num_params == PARSER_MAX_PARAMS - 1) {
    // We've filled the last usable slot; set sentinel to stop accumulation.
    p->num_params = PARSER_MAX_PARAMS;
  }
}

// ---------------------------------------------------------------------------
// Append a digit to the current parameter.
// ---------------------------------------------------------------------------
static void accum_param(parser_t *p, char digit) {
  int idx = p->num_params;
  if (idx >= PARSER_MAX_PARAMS)
    return;
  int val = p->params[idx];
  if (val < 0)
    val = 0;
  val = val * 10 + (digit - '0');
  if (val > 999999)
    val = 999999;
  p->params[idx] = val;
}

// ---------------------------------------------------------------------------
// Append an intermediate byte.
// ---------------------------------------------------------------------------
static void accum_intermediate(parser_t *p, char b) {
  if (p->num_intermediates < PARSER_MAX_INTERMEDIATES) {
    p->intermediates[p->num_intermediates++] = b;
  }
}

// ---------------------------------------------------------------------------
// OSC helpers
// ---------------------------------------------------------------------------
static void osc_append(parser_t *p, char b) {
  if (p->osc_len < (int)(sizeof(p->osc_string) - 1))
    p->osc_string[p->osc_len++] = b;
}

static void osc_dispatch(parser_t *p) {
  p->osc_string[p->osc_len] = '\0';
  // Parse the command number from the string (digits before first ';').
  int command = -1;
  int i = 0;
  if (p->osc_len > 0 && p->osc_string[0] >= '0' && p->osc_string[0] <= '9') {
    command = 0;
    while (i < p->osc_len && p->osc_string[i] >= '0' &&
           p->osc_string[i] <= '9') {
      command = command * 10 + (p->osc_string[i] - '0');
      i++;
    }
    // Skip the separator ';' if present.
    if (i < p->osc_len && p->osc_string[i] == ';')
      i++;
  }
  const char *str = (i < p->osc_len) ? &p->osc_string[i] : "";
  if (p->callbacks.on_osc)
    p->callbacks.on_osc(command, str, p->ctx);
}

// ---------------------------------------------------------------------------
// DCS helpers
// ---------------------------------------------------------------------------
static void dcs_append(parser_t *p, char b) {
  if (p->dcs_len < (int)(sizeof(p->dcs_string) - 1))
    p->dcs_string[p->dcs_len++] = b;
}

static void dcs_dispatch(parser_t *p) {
  p->dcs_string[p->dcs_len] = '\0';
  if (p->callbacks.on_dcs)
    p->callbacks.on_dcs(p->params[0], p->dcs_string, p->ctx);
}

// ---------------------------------------------------------------------------
// String helpers (SOS/PM/APC)
// ---------------------------------------------------------------------------
static void string_append(parser_t *p, char b) {
  if (p->string_len < (int)(sizeof(p->string_buf) - 1))
    p->string_buf[p->string_len++] = b;
}

static void string_dispatch(parser_t *p) {
  p->string_buf[p->string_len] = '\0';
  if (p->callbacks.on_string)
    p->callbacks.on_string(p->string_buf, p->ctx);
}

// ---------------------------------------------------------------------------
// Ground state handler
// ---------------------------------------------------------------------------
static void ground(parser_t *p, uint8_t b) {
  switch (b) {
  case 0x00 ... 0x17:
  case 0x19:
  case 0x1C ... 0x1F:
    if (p->callbacks.on_execute)
      p->callbacks.on_execute((char)b, p->ctx);
    break;
  case 0x18: // CAN
  case 0x1A: // SUB
    if (p->callbacks.on_execute)
      p->callbacks.on_execute((char)b, p->ctx);
    // remain in GROUND
    break;
  case 0x1B: // ESC
    p->state = PARSER_ESC;
    reset_accumulators(p);
    break;
  case 0x20 ... 0x7E:
    if (p->callbacks.on_print)
      p->callbacks.on_print((char)b, p->ctx);
    break;
  case 0x7F: // DEL – ignore
    break;
  // 8-bit C1 controls (we handle 7-bit equivalents via ESC, but accept
  // these for robustness).
  case 0x84 ... 0x87: // IND, NEL, HTS, etc.
  case 0x89 ... 0x8B:
  case 0x8D:
  case 0x8E:
  case 0x8F: // PU1 – treat as execute
    if (p->callbacks.on_execute)
      p->callbacks.on_execute((char)b, p->ctx);
    break;
  case 0x90: // DCS (8-bit)
    p->state = PARSER_DCS;
    reset_accumulators(p);
    p->dcs_len = 0;
    break;
  case 0x98: // SOS (8-bit)
    p->state = PARSER_SOS_PM_APC_STRING;
    p->string_len = 0;
    break;
  case 0x9B: // CSI (8-bit)
    p->state = PARSER_CSI_PARAM;
    reset_accumulators(p);
    break;
  case 0x9D: // OSC (8-bit)
    p->state = PARSER_OSC;
    p->osc_len = 0;
    break;
  case 0x9E: // PM (8-bit)
  case 0x9F: // APC (8-bit)
    p->state = PARSER_SOS_PM_APC_STRING;
    p->string_len = 0;
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------------
// ESC state handler – received 0x1B, waiting for the next byte.
// ---------------------------------------------------------------------------
static void esc_state(parser_t *p, uint8_t b) {
  switch (b) {
  case 0x00 ... 0x17:
  case 0x19:
  case 0x1C ... 0x1F:
    if (p->callbacks.on_execute)
      p->callbacks.on_execute((char)b, p->ctx);
    break;
  case 0x18: // CAN
  case 0x1A: // SUB
    if (p->callbacks.on_execute)
      p->callbacks.on_execute((char)b, p->ctx);
    p->state = PARSER_GROUND;
    break;
  case 0x1B: // ESC ESC – second ESC resets
    reset_accumulators(p);
    break;
  case 0x20 ... 0x2F: // intermediate bytes
    accum_intermediate(p, (char)b);
    break;
  case '[': // CSI introducer
    p->state = PARSER_CSI_PARAM;
    reset_accumulators(p);
    break;
  case ']': // OSC introducer
    p->state = PARSER_OSC;
    p->osc_len = 0;
    break;
  case 'P': // DCS introducer
    p->state = PARSER_DCS;
    reset_accumulators(p);
    p->dcs_len = 0;
    break;
  case 'X': // SOS
    p->state = PARSER_SOS_PM_APC_STRING;
    p->string_len = 0;
    break;
  case '^': // PM
    p->state = PARSER_SOS_PM_APC_STRING;
    p->string_len = 0;
    break;
  case '_': // APC
    p->state = PARSER_SOS_PM_APC_STRING;
    p->string_len = 0;
    break;
  default:
    if (b >= 0x30 && b <= 0x7E) {
      // Any byte in 0x30-0x7E not already handled above (like 'P', 'X',
      // '[', ']', '^', '_') is treated as a final byte for an ESC sequence.
      char final = (char)b;
      if (p->callbacks.on_esc)
        p->callbacks.on_esc(p->intermediates, p->num_intermediates, final,
                            p->ctx);
      p->state = PARSER_GROUND;
    } else {
      // Unexpected byte – reset to ground.
      p->state = PARSER_GROUND;
    }
    break;
  }
}

// ---------------------------------------------------------------------------
// CSI param state – collecting parameter bytes and private markers.
// ---------------------------------------------------------------------------
static void csi_param(parser_t *p, uint8_t b) {
  switch (b) {
  case 0x00 ... 0x17:
  case 0x19:
  case 0x1C ... 0x1F:
    if (p->callbacks.on_execute)
      p->callbacks.on_execute((char)b, p->ctx);
    break;
  case 0x18: // CAN
  case 0x1A: // SUB
    if (p->callbacks.on_execute)
      p->callbacks.on_execute((char)b, p->ctx);
    p->state = PARSER_GROUND;
    break;
  case 0x1B: // ESC – restart ESC sequence
    p->state = PARSER_ESC;
    reset_accumulators(p);
    break;
  case 0x20 ... 0x2F: // intermediate bytes – transition to CSI_INTERMEDIATE
    accum_intermediate(p, (char)b);
    p->state = PARSER_CSI_INTERMEDIATE;
    break;
  case '0' ... '9': // digit
    accum_param(p, (char)b);
    break;
  case ';': // parameter separator
    next_param(p);
    break;
  case ':': // sub-parameter separator (ignore for now)
    break;
  case '<':
  case '=':
  case '>':
  case '?': // private marker – store as intermediate
    accum_intermediate(p, (char)b);
    break;
  case 0x40 ... 0x7E: // final byte
  {
    int param_count = p->num_params + 1;
    if (param_count > PARSER_MAX_PARAMS)
      param_count = PARSER_MAX_PARAMS;
    if (p->callbacks.on_csi)
      p->callbacks.on_csi(p->params, param_count, p->intermediates,
                          p->num_intermediates, (char)b, p->ctx);
    p->state = PARSER_GROUND;
    break;
  }
  default:
    p->state = PARSER_GROUND;
    break;
  }
}

// ---------------------------------------------------------------------------
// CSI intermediate state – collecting intermediate bytes before final.
// ---------------------------------------------------------------------------
static void csi_intermediate(parser_t *p, uint8_t b) {
  switch (b) {
  case 0x00 ... 0x17:
  case 0x19:
  case 0x1C ... 0x1F:
    if (p->callbacks.on_execute)
      p->callbacks.on_execute((char)b, p->ctx);
    break;
  case 0x18: // CAN
  case 0x1A: // SUB
    if (p->callbacks.on_execute)
      p->callbacks.on_execute((char)b, p->ctx);
    p->state = PARSER_GROUND;
    break;
  case 0x1B: // ESC
    p->state = PARSER_ESC;
    reset_accumulators(p);
    break;
  case 0x20 ... 0x2F: // intermediate bytes
    accum_intermediate(p, (char)b);
    break;
  case 0x30 ... 0x3F: // param bytes – go back to param state
    // Some terminals allow params after intermediates; we go back.
    // But the first char here would need to start a param.
    // For simplicity, transition back to param state.
    p->state = PARSER_CSI_PARAM;
    // Process this byte as a param.
    csi_param(p, b);
    break;
  case 0x40 ... 0x7E: // final byte
  {
    int param_count = p->num_params + 1;
    if (param_count > PARSER_MAX_PARAMS)
      param_count = PARSER_MAX_PARAMS;
    if (p->callbacks.on_csi)
      p->callbacks.on_csi(p->params, param_count, p->intermediates,
                          p->num_intermediates, (char)b, p->ctx);
    p->state = PARSER_GROUND;
    break;
  }
  default:
    p->state = PARSER_GROUND;
    break;
  }
}

// ---------------------------------------------------------------------------
// OSC state – collecting OSC string.
// ---------------------------------------------------------------------------
static void osc_state(parser_t *p, uint8_t b) {
  switch (b) {
  case 0x07: // BEL terminates OSC
    osc_dispatch(p);
    p->state = PARSER_GROUND;
    break;
  case 0x18: // CAN
  case 0x1A: // SUB
    if (p->callbacks.on_execute)
      p->callbacks.on_execute((char)b, p->ctx);
    p->state = PARSER_GROUND;
    break;
  case 0x9C: // ST (8-bit)
    osc_dispatch(p);
    p->state = PARSER_GROUND;
    break;
  case 0x1B: // ESC – store in string; ST (ESC \) is detected in parser_advance
    osc_append(p, (char)0x1B);
    break;
  default:
    osc_append(p, (char)b);
    break;
  }
}

// ---------------------------------------------------------------------------
// DCS state – collecting DCS string.
// ---------------------------------------------------------------------------
static void dcs_state(parser_t *p, uint8_t b) {
  switch (b) {
  case 0x07: // BEL terminates
    dcs_dispatch(p);
    p->state = PARSER_GROUND;
    break;
  case 0x18: // CAN
  case 0x1A: // SUB
    if (p->callbacks.on_execute)
      p->callbacks.on_execute((char)b, p->ctx);
    p->state = PARSER_GROUND;
    break;
  case 0x9C: // ST (8-bit)
    dcs_dispatch(p);
    p->state = PARSER_GROUND;
    break;
  case 0x1B: // ESC – might be ST
    // Similar to OSC, store ESC and wait for '\'
    dcs_append(p, (char)0x1B);
    break;
  case 0x20 ... 0x7E:
    dcs_append(p, (char)b);
    break;
  default:
    dcs_append(p, (char)b);
    break;
  }
}

// ---------------------------------------------------------------------------
// SOS/PM/APC string state
// ---------------------------------------------------------------------------
static void sos_pm_apc_string(parser_t *p, uint8_t b) {
  switch (b) {
  case 0x07: // BEL terminates
    string_dispatch(p);
    p->state = PARSER_GROUND;
    break;
  case 0x18: // CAN
  case 0x1A: // SUB
    if (p->callbacks.on_execute)
      p->callbacks.on_execute((char)b, p->ctx);
    p->state = PARSER_GROUND;
    break;
  case 0x9C: // ST (8-bit)
    string_dispatch(p);
    p->state = PARSER_GROUND;
    break;
  case 0x1B: // ESC – might be ST
    string_append(p, (char)0x1B);
    break;
  case 0x20 ... 0x7E:
    string_append(p, (char)b);
    break;
  default:
    string_append(p, (char)b);
    break;
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void parser_init(parser_t *p, const parser_callbacks_t *callbacks, void *ctx) {
  memset(p, 0, sizeof(*p));
  p->state = PARSER_GROUND;
  if (callbacks)
    p->callbacks = *callbacks;
  p->ctx = ctx;
  reset_accumulators(p);
}

void parser_reset(parser_t *p) {
  p->state = PARSER_GROUND;
  reset_accumulators(p);
  p->osc_len = 0;
  p->dcs_len = 0;
  p->string_len = 0;
}

void parser_advance(parser_t *p, uint8_t b) {
  // Handle ST (string terminator ESC \) by checking if we're in a
  // string-collecting state and the previous byte was ESC (0x1B).
  // We check all states because OSC/DCS/SOS/PM/APC all use ST.
  // This is a simple approach: we handle the two-byte ST sequence
  // by looking back. Since we process byte by byte, we need to check
  // if the last character in the buffer is 0x1B and this is '\\'.

  switch (p->state) {
  case PARSER_GROUND:
    ground(p, b);
    break;
  case PARSER_ESC:
    esc_state(p, b);
    break;
  case PARSER_CSI:
    // CSI without param accumulation – transition to param state.
    // This state exists for robustness (e.g. 8-bit CSI entry).
    p->state = PARSER_CSI_PARAM;
    reset_accumulators(p);
    csi_param(p, b);
    break;
  case PARSER_CSI_PARAM:
    csi_param(p, b);
    break;
  case PARSER_CSI_INTERMEDIATE:
    csi_intermediate(p, b);
    break;
   case PARSER_OSC: {
    // Check for ST terminator (ESC \)
    // If the buffer was full and ESC was dropped, a backslash now also
    // counts as ST.
    bool osc_esc_stored = (p->osc_len > 0 &&
                           p->osc_string[p->osc_len - 1] == 0x1B);
    bool osc_esc_dropped =
        (p->osc_len >= (int)(sizeof(p->osc_string) - 1));
    if (b == '\\' && (osc_esc_stored || osc_esc_dropped)) {
      if (osc_esc_stored)
        p->osc_len--;
      osc_dispatch(p);
      p->state = PARSER_GROUND;
      break;
    }
    osc_state(p, b);
    break;
  }
  case PARSER_DCS: {
    bool dcs_esc_stored = (p->dcs_len > 0 &&
                           p->dcs_string[p->dcs_len - 1] == 0x1B);
    bool dcs_esc_dropped =
        (p->dcs_len >= (int)(sizeof(p->dcs_string) - 1));
    if (b == '\\' && (dcs_esc_stored || dcs_esc_dropped)) {
      if (dcs_esc_stored)
        p->dcs_len--;
      dcs_dispatch(p);
      p->state = PARSER_GROUND;
      break;
    }
    dcs_state(p, b);
    break;
  }
  case PARSER_SOS_PM_APC_STRING: {
    bool str_esc_stored = (p->string_len > 0 &&
                           p->string_buf[p->string_len - 1] == 0x1B);
    bool str_esc_dropped =
        (p->string_len >= (int)(sizeof(p->string_buf) - 1));
    if (b == '\\' && (str_esc_stored || str_esc_dropped)) {
      if (str_esc_stored)
        p->string_len--;
      string_dispatch(p);
      p->state = PARSER_GROUND;
      break;
    }
    sos_pm_apc_string(p, b);
    break;
  }
  default:
    // Unknown state – reset.
    p->state = PARSER_GROUND;
    break;
  }
}

void parser_feed(parser_t *p, const uint8_t *buf, size_t len) {
  for (size_t i = 0; i < len; i++)
    parser_advance(p, buf[i]);
}
