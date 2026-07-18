/* dicTerm - Font Rendering Subsystem with Nerd Fonts support */
#ifndef FONT_H
#define FONT_H

#include <raylib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// -------------------------------------------------------------------------
//  Font Rendering Subsystem
//
//  Provides glyph-atlas-based font rendering using raylib's LoadFontEx()
//  and DrawTextCodepoints() for high-performance terminal text.
//
//  Features:
//    - TrueType/OpenType font loading via raylib
//    - Standard + Nerd Fonts variant with PUA icon codepoints
//    - Codepoint index cache for fast glyph lookup
//    - Fixed-width terminal rendering with per-character spacing
//    - Hot-swappable font selection
// -------------------------------------------------------------------------

/// Maximum number of codepoints we register when loading a font.
#define FONT_MAX_CODEPOINTS 4096

/// Maximum length of a font file path.
#define FONT_PATH_MAX 512

/// Nerd Fonts Private Use Area ranges (icons, powerline, devicons, etc.)
/// These are loaded when font_select("nerd") is called.
#define FONT_NERD_RANGES_COUNT 4
static const int FONT_NERD_RANGES[FONT_NERD_RANGES_COUNT][2] = {
    { 0xE000, 0xEFFF },  // Powerline, Devicons, Font Awesome, Pomicons
    { 0xF000, 0xF2E0 },  // Font Awesome continuation (brands)
    { 0xF500, 0xFD46 },  // Material Design Icons
    { 0x2600, 0x27BF },  // Misc symbols, dingbats, etc.
};

/// Default ASCII + extended range for standard fonts.
#define FONT_ASCII_MIN   0x20
#define FONT_ASCII_MAX   0x7E

// -------------------------------------------------------------------------
//  Glyph metrics cache entry
// -------------------------------------------------------------------------
typedef struct {
    int      codepoint;   // Unicode codepoint value
    int      glyphIndex;  // Raylib's internal glyph index (-1 if missing)
    int      advanceX;    // Horizontal advance in pixels
    Rectangle atlasRec;   // Rectangle in the font atlas texture
    bool     valid;       // Whether this entry has been resolved
} font_glyph_entry_t;

// -------------------------------------------------------------------------
//  Main font handle
// -------------------------------------------------------------------------
typedef struct {
    Font       regular;         // Standard ASCII-range font
    Font       nerd;            // Nerd Fonts variant (extended codepoints)
    Font      *current;         // Pointer to the active Font (regular or nerd)
    bool       is_nerd;         // Whether Nerd Font variant is active

    float      font_size;       // Font size in pixels (height)
    float      char_width;      // Fixed character width (derived from advanceX)
    float      char_height;     // Character height in pixels
    float      spacing;         // Spacing between characters

    int        ascent;          // Font ascent in pixels
    int        descent;         // Font descent in pixels

    // Glyph index cache for quick lookups.
    // Map from codepoint → font_glyph_entry_t.
    font_glyph_entry_t *glyph_cache;
    int                 glyph_cache_capacity;

    // Font file paths (for reloading / switching)
    char regular_path[FONT_PATH_MAX];
    char nerd_path[FONT_PATH_MAX];

    bool initialized;
} font_handle_t;

// -------------------------------------------------------------------------
//  Color configuration
// -------------------------------------------------------------------------
typedef struct {
    Color foreground;
    Color background;
} font_color_t;

// -------------------------------------------------------------------------
//  API
// -------------------------------------------------------------------------

/// Initialize the font subsystem.
/// Loads a standard font from `config->path` (or a built-in fallback).
/// If `config->nerd_path` is provided, also loads the Nerd Font variant.
/// Returns an initialized handle, or NULL on failure.
font_handle_t* font_init(const char *font_path, const char *nerd_path,
                         float font_size);

/// Uninitialize and free all font resources.
void font_uninit(font_handle_t *handle);

/// Select which font variant to use for rendering.
/// @param name  "regular" for standard font, "nerd" for Nerd Fonts variant.
/// @return true on success, false if the variant isn't loaded.
bool font_select(font_handle_t *handle, const char *name);

/// Return true if the Nerd Font variant is currently active.
bool font_is_nerd(const font_handle_t *handle);

/// Get the glyph entry for a codepoint (cached).
/// Returns a pointer to the cached entry, or NULL if not found.
const font_glyph_entry_t* font_get_glyph(font_handle_t *handle, int codepoint);

/// Render a line of the screen buffer (ASCII chars) to the current window.
/// Each row is `cols` wide.  The line is rendered starting at pixel position
/// (x, y).  Uses `DrawTextCodepoints` with codepoint arrays.
void font_render_line(font_handle_t *handle, const char *line, int cols,
                      float x, float y, Color fg, Color bg);

/// Measure how many pixels wide a given text would be at the current font
/// settings.  Returns the width in pixels.
float font_measure_text(font_handle_t *handle, const char *text, int len);

/// Get the fixed character width in pixels.
float font_char_width(const font_handle_t *handle);

/// Get the character height in pixels.
float font_char_height(const font_handle_t *handle);

/// Build a list of codepoints covering ASCII + extended ranges we care about.
/// Returns the number of codepoints written to `out` (max `out_sz`).
int font_build_codepoint_list(int *out, int out_sz, bool include_nerd);

/// Find a Nerd Fonts .ttf file on the system using common paths.
/// Returns a pointer to a static buffer with the path, or NULL if not found.
const char* font_find_nerd_path(void);

/// Find a regular monospace font .ttf on the system.
/// Returns a pointer to a static buffer with the path, or NULL for fallback.
const char* font_find_regular_path(void);

#endif // FONT_H
