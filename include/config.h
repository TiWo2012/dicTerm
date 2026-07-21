/**
 * @file config.h
 * @brief Terminal emulator configuration.
 *
 * Loads settings from a Unix-style ".conf" file of flat "key = value" lines
 * (no INI sections).
 * Search order (first existing file wins):
 *   1. $XDG_CONFIG_HOME/dicTerm/dicTerm.conf
 *   2. ~/.config/dicTerm/dicTerm.conf
 *   3. /etc/dicTerm.conf
 *   4. Built-in defaults
 *
 * All values have sensible defaults so the config file is optional.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
//  Config struct – all fields filled by config_load()
// ---------------------------------------------------------------------------

/** @brief Runtime configuration loaded from file + defaults. */
typedef struct {
  // ── Terminal geometry ──────────────────────────────────────────────────
  int   rows;               /**< Visible terminal rows (default 36). */
  int   cols;               /**< Visible terminal columns (default 100). */
  int   scrollback_capacity; /**< Max scrollback lines (default 1000). */

  // ── Window ────────────────────────────────────────────────────────────
  int   win_width;          /**< Initial window width in px (default 1280). */
  int   win_height;         /**< Initial window height in px (default 800). */
  int   win_padding;        /**< Padding around terminal area (default 10). */

  // ── Font ──────────────────────────────────────────────────────────────
  float font_size;          /**< Font point size (default 20.0). */
  char  font_regular[512];  /**< Path to regular font (empty = auto-discover). */
  char  font_nerd[512];     /**< Path to Nerd Font (empty = auto-discover).  */
  char  font_symbols[512];  /**< Path to symbol font (empty = auto-discover). */

  // ── Default colours ───────────────────────────────────────────────────
  uint8_t default_fg[3];    /**< Default foreground RGB (default 220,220,220). */
  uint8_t default_bg[3];    /**< Default background RGB (default 0,0,0). */
  bool    clear_bg;          /**< Transparent background (default true). */
  float   bg_opacity;        /**< Background opacity 0–1 when clear_bg is on (default 0.5). */
  bool    bg_blur;           /**< Request compositor blur behind window (default false). */
} dicterm_config_t;

// ---------------------------------------------------------------------------
//  API
// ---------------------------------------------------------------------------

/**
 * @brief Load configuration from the default search path.
 *
 * Starts with hard-coded defaults, then overlays values from the first
 * config file found (see search order above).  Fields not present in
 * the file retain their defaults.
 *
 * @return Populated config struct (always succeeds – falls back to defaults).
 */
dicterm_config_t config_load(void);

/**
 * @brief Load configuration from an explicit path.
 *
 * Same as config_load() but reads from @p path instead of searching.
 * If @p path cannot be opened the config is left at defaults.
 *
 * @param cfg  [out] Config struct to populate.
 * @param path Absolute or relative path to config file.
 * @return     true if the file was parsed successfully, false otherwise.
 */
bool config_load_from(dicterm_config_t *cfg, const char *path);

#endif // CONFIG_H
