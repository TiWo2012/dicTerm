/**
 * @file config.c
 * @brief Configuration file parser implementation.
 *
 * Reads a simple Unix-style ".conf" file of flat "key = value" lines
 * (no INI sections).  Comments start with '#' or ';' and run to end of
 * line.  Leading/trailing whitespace is stripped from keys and values.
 * Unknown keys are silently ignored so the file is forward-compatible.
 */
#define _DEFAULT_SOURCE
#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
//  Defaults
// ---------------------------------------------------------------------------

#define DEFAULT_ROWS               36
#define DEFAULT_COLS               100
#define DEFAULT_SCROLLBACK_CAPACITY 1000
#define DEFAULT_WIN_WIDTH          1280
#define DEFAULT_WIN_HEIGHT         800
#define DEFAULT_WIN_PADDING        10
#define DEFAULT_FONT_SIZE          11.0f
#define DEFAULT_FG_R 220
#define DEFAULT_FG_G 220
#define DEFAULT_FG_B 220
#define DEFAULT_BG_R   0
#define DEFAULT_BG_G   0
#define DEFAULT_BG_B   0
#define DEFAULT_CLEAR_BG true
#define DEFAULT_BG_OPACITY 0.5f
#define DEFAULT_BG_BLUR true

static void set_defaults(dicterm_config_t *cfg) {
  cfg->rows               = DEFAULT_ROWS;
  cfg->cols               = DEFAULT_COLS;
  cfg->scrollback_capacity = DEFAULT_SCROLLBACK_CAPACITY;
  cfg->win_width          = DEFAULT_WIN_WIDTH;
  cfg->win_height         = DEFAULT_WIN_HEIGHT;
  cfg->win_padding        = DEFAULT_WIN_PADDING;
  cfg->font_size          = DEFAULT_FONT_SIZE;
  cfg->font_regular[0]    = '\0';
  cfg->font_nerd[0]       = '\0';
  cfg->font_symbols[0]    = '\0';
  cfg->default_fg[0]      = DEFAULT_FG_R;
  cfg->default_fg[1]      = DEFAULT_FG_G;
  cfg->default_fg[2]      = DEFAULT_FG_B;
  cfg->default_bg[0]      = DEFAULT_BG_R;
  cfg->default_bg[1]      = DEFAULT_BG_G;
  cfg->default_bg[2]      = DEFAULT_BG_B;
  cfg->clear_bg           = DEFAULT_CLEAR_BG;
  cfg->bg_opacity         = DEFAULT_BG_OPACITY;
  cfg->bg_blur            = DEFAULT_BG_BLUR;
}

// ---------------------------------------------------------------------------
//  INI-style parser helpers
// ---------------------------------------------------------------------------

/** @brief Strip leading and trailing whitespace in-place. */
static char *trim(char *s) {
  if (!s) return s;
  // Skip leading whitespace
  while (*s && (unsigned char)*s <= ' ') s++;
  if (*s == '\0') return s;
  // Chop trailing whitespace
  char *end = s + strlen(s) - 1;
  while (end > s && (unsigned char)*end <= ' ') end--;
  *(end + 1) = '\0';
  return s;
}

/** @brief Parse "R,G,B" into an RGB triplet.  Returns true on success. */
static bool parse_rgb(const char *s, uint8_t rgb[3]) {
  int r = 0, g = 0, b = 0;
  if (sscanf(s, " %d , %d , %d", &r, &g, &b) != 3) return false;
  if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return false;
  rgb[0] = (uint8_t)r;
  rgb[1] = (uint8_t)g;
  rgb[2] = (uint8_t)b;
  return true;
}

// ---------------------------------------------------------------------------
//  Flat key dispatcher (Unix .conf style: "key = value", no sections)
// ---------------------------------------------------------------------------

/** @brief Apply a single key=value pair to the config struct. */
static void apply_key(dicterm_config_t *cfg, const char *key, const char *val) {
  int ival = 0;
  float fval = 0.0f;

  if (strcmp(key, "rows") == 0) {
    ival = atoi(val);
    if (ival > 0) cfg->rows = ival;
  } else if (strcmp(key, "cols") == 0) {
    ival = atoi(val);
    if (ival > 0) cfg->cols = ival;
  } else if (strcmp(key, "scrollback") == 0) {
    ival = atoi(val);
    if (ival >= 0) cfg->scrollback_capacity = ival;
  } else if (strcmp(key, "font_size") == 0) {
    fval = (float)atof(val);
    if (fval > 0.0f) cfg->font_size = fval;
  } else if (strcmp(key, "window_width") == 0) {
    ival = atoi(val);
    if (ival > 0) cfg->win_width = ival;
  } else if (strcmp(key, "window_height") == 0) {
    ival = atoi(val);
    if (ival > 0) cfg->win_height = ival;
  } else if (strcmp(key, "window_padding") == 0) {
    ival = atoi(val);
    if (ival >= 0) cfg->win_padding = ival;
  } else if (strcmp(key, "font_regular") == 0) {
    strncpy(cfg->font_regular, val, sizeof(cfg->font_regular) - 1);
    cfg->font_regular[sizeof(cfg->font_regular) - 1] = '\0';
  } else if (strcmp(key, "font_nerd") == 0) {
    strncpy(cfg->font_nerd, val, sizeof(cfg->font_nerd) - 1);
    cfg->font_nerd[sizeof(cfg->font_nerd) - 1] = '\0';
  } else if (strcmp(key, "font_symbols") == 0) {
    strncpy(cfg->font_symbols, val, sizeof(cfg->font_symbols) - 1);
    cfg->font_symbols[sizeof(cfg->font_symbols) - 1] = '\0';
  } else if (strcmp(key, "foreground") == 0) {
    parse_rgb(val, cfg->default_fg);
  } else if (strcmp(key, "background") == 0) {
    parse_rgb(val, cfg->default_bg);
  } else if (strcmp(key, "clear_bg") == 0) {
    cfg->clear_bg = (strcmp(val, "true") == 0 || strcmp(val, "yes") == 0 ||
                     strcmp(val, "1") == 0);
  } else if (strcmp(key, "bg_opacity") == 0) {
    fval = (float)atof(val);
    if (fval >= 0.0f && fval <= 1.0f) cfg->bg_opacity = fval;
  } else if (strcmp(key, "bg_blur") == 0) {
    cfg->bg_blur = (strcmp(val, "true") == 0 || strcmp(val, "yes") == 0 ||
                    strcmp(val, "1") == 0);
  }
  // Unknown keys are silently ignored.
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

bool config_load_from(dicterm_config_t *cfg, const char *path) {
  if (!cfg || !path) return false;

  // Always start from defaults so callers get a usable struct even when
  // the file cannot be opened or is empty.
  set_defaults(cfg);

  FILE *f = fopen(path, "r");
  if (!f) return false;

  char buf[1024];

  while (fgets(buf, (int)sizeof(buf), f)) {
    char *line = trim(buf);
    if (!line || *line == '\0' || *line == '#' || *line == ';')
      continue;

    // Skip INI-style section headers (no longer used, tolerated for compat).
    if (*line == '[') continue;

    // Key = value
    char *eq = strchr(line, '=');
    if (!eq) continue;

    *eq = '\0';
    char *key = trim(line);
    char *val = trim(eq + 1);
    if (*key == '\0') continue;

    apply_key(cfg, key, val);
  }

  fclose(f);
  return true;
}

/// @brief Build the candidate config paths and return the first that exists,
///        or NULL if none are found.  @p out must hold at least 4 paths.
static const char *first_existing(const char *candidates[], int n) {
  for (int i = 0; i < n; i++)
    if (candidates[i]) {
      struct stat st;
      if (stat(candidates[i], &st) == 0) return candidates[i];
    }
  return NULL;
}

dicterm_config_t config_load(void) {
  dicterm_config_t cfg;
  set_defaults(&cfg);

  // Search order (first existing file wins):
  //   1. $XDG_CONFIG_HOME/dicTerm/dicTerm.conf
  //   2. ~/.config/dicTerm/dicTerm.conf
  //   3. /etc/dicTerm.conf
  char xdg_path[1024];
  char home_path[1024];
  const char *home = getenv("HOME");
  const char *xdg = getenv("XDG_CONFIG_HOME");

  if (xdg && *xdg)
    snprintf(xdg_path, sizeof(xdg_path), "%s/dicTerm/dicTerm.conf", xdg);
  else
    xdg_path[0] = '\0';

  if (home && *home)
    snprintf(home_path, sizeof(home_path), "%s/.config/dicTerm/dicTerm.conf", home);
  else
    home_path[0] = '\0';

  const char *candidates[] = { xdg_path, home_path, "/etc/dicTerm.conf" };
  const char *found = first_existing(candidates, 3);
  if (found) config_load_from(&cfg, found);

  return cfg;
}
