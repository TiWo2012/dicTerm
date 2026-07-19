#ifndef SCREEN_H
#define SCREEN_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @file screen.h
 * @brief Screen cell and buffer management with SGR colour attributes.
 *
 * Provides the per-cell data structure used throughout the terminal
 * emulator to store character glyphs together with their SGR (Select
 * Graphic Rendition) colour and style attributes.  Also provides a
 * simple row-major buffer abstraction over a grid of cells.
 */

// ---------------------------------------------------------------------------
// Screen cell with full SGR (Select Graphic Rendition) attributes.
//
// Each cell in the terminal grid stores:
//   - The character byte (7-bit ASCII or UTF-8 leader)
//   - Foreground colour (RGB, each 0..255; 0 = default/reset)
//   - Background colour (RGB, each 0..255; 0 = transparent/default)
//   - Bold, italic, underline, blink flags (for future use)
// ---------------------------------------------------------------------------

/**
 * @brief A single terminal cell with its SGR attributes.
 *
 * Every cell on screen is represented by one of these structures.
 * Colour components are stored as 8-bit RGB values.  A foreground of
 * {0,0,0} means "use the terminal default foreground"; a background
 * of {0,0,0} means "transparent" (the window background shows through).
 *
 * The character is stored as a decoded Unicode codepoint (int).
 * Values 0-127 represent ASCII; values > 127 represent arbitrary
 * Unicode including Nerd Font PUA icons.
 */
typedef struct {
  int   ch;                 ///< Unicode codepoint (decoded from UTF-8).
  uint8_t fg[3];            ///< RGB foreground colour (0 = default).
  uint8_t bg[3];            ///< RGB background colour (0 = transparent).
  uint8_t bold : 1;         ///< Bold attribute.
  uint8_t italic : 1;       ///< Italic attribute.
  uint8_t underline : 1;    ///< Underline attribute.
  uint8_t blink : 1;        ///< Blink attribute.
} screen_cell_t;

/**
 * @brief Pack foreground colour into a single uint32_t.
 * @param c  Pointer to a screen_cell_t.
 * @return   Colour as 0x00RRGGBB.
 */
static inline uint32_t screen_cell_fg_u32(const screen_cell_t *c) {
  return ((uint32_t)c->fg[0] << 16) |
         ((uint32_t)c->fg[1] <<  8) |
         ((uint32_t)c->fg[2]);
}

/**
 * @brief Pack background colour into a single uint32_t.
 * @param c  Pointer to a screen_cell_t.
 * @return   Colour as 0x00RRGGBB.
 */
static inline uint32_t screen_cell_bg_u32(const screen_cell_t *c) {
  return ((uint32_t)c->bg[0] << 16) |
         ((uint32_t)c->bg[1] <<  8) |
         ((uint32_t)c->bg[2]);
}

// ---------------------------------------------------------------------------
// Screen buffer – a simple rows×cols grid of cells.
// ---------------------------------------------------------------------------

/**
 * @brief A rows × cols grid of screen_cell_t values.
 *
 * Cells are stored in row-major order: the cell at (r,c) lives at
 * `cells[r * cols + c]`.
 */
typedef struct {
  screen_cell_t *cells;     ///< Flat row-major array.
  int rows;                 ///< Number of rows.
  int cols;                 ///< Number of columns.
} screen_buf_t;

/**
 * @brief Allocate and initialise a screen buffer.
 *
 * Every cell is set to space (0x20) with default fg = {220,220,220}
 * and bg = {0,0,0} (transparent).  Style flags are all cleared.
 *
 * @param rows  Number of rows (must be > 0).
 * @param cols  Number of columns (must be > 0).
 * @return      Pointer to the new screen_buf_t, or NULL on allocation failure.
 */
screen_buf_t* screen_buf_new(int rows, int cols);

/**
 * @brief Free a screen buffer previously created with screen_buf_new().
 * @param sb  Buffer to free (NULL-safe).
 */
void screen_buf_free(screen_buf_t *sb);

/**
 * @brief Resize an existing screen buffer to new dimensions.
 *
 * Reallocates the cell array and preserves existing content where
 * the old and new grids overlap. New cells are filled with spaces
 * using default colours. If new dimensions equal current dimensions,
 * the function is a no-op.
 *
 * @param sb       Buffer to resize.
 * @param new_rows New number of rows (must be > 0).
 * @param new_cols New number of columns (must be > 0).
 * @return         true on success, false on allocation failure.
 */
bool screen_buf_resize(screen_buf_t *sb, int new_rows, int new_cols);

/**
 * @brief Write one character into a cell.
 *
 * The cell's colour and style attributes are NOT modified; only the
 * character byte is replaced.  This lets SGR state persist across
 * multiple writes to the same cell.
 *
 * @param sb   Target screen buffer.
 * @param row  Row index (0-based).
 * @param col  Column index (0-based).
 * @param ch   Unicode codepoint to write.
 */
void screen_buf_put(screen_buf_t *sb, int row, int col, int ch);

/**
 * @brief Return a pointer to the cell at (row, col).
 *
 * @param sb   Target screen buffer.
 * @param row  Row index (0-based).
 * @param col  Column index (0-based).
 * @return     Pointer to the cell, or NULL if row/col are out of range.
 */
screen_cell_t* screen_buf_cell(screen_buf_t *sb, int row, int col);

/**
 * @brief Apply SGR colour to a specific cell.
 *
 * Overwrites the cell's foreground and background RGB values.
 * Style flags (bold, italic, etc.) are not touched.
 *
 * @param sb   Target screen buffer.
 * @param row  Row index.
 * @param col  Column index.
 * @param fg   New foreground colour (3 bytes, may be NULL to leave unchanged).
 * @param bg   New background colour (3 bytes, may be NULL to leave unchanged).
 */
void screen_buf_apply_sgr(screen_buf_t *sb, int row, int col,
                          const uint8_t fg[3], const uint8_t bg[3]);

/**
 * @brief Fill an entire row with spaces.
 *
 * @param sb             Target screen buffer.
 * @param row            Row to clear.
 * @param reset_colours  If true, also reset fg/bg to defaults and clear flags.
 */
void screen_buf_clear_row(screen_buf_t *sb, int row, bool reset_colours);

/**
 * @brief Erase parts of the display (ED – CSI J).
 *
 * Modes:
 *   - 0: cursor → end of screen
 *   - 1: start → cursor
 *   - 2: entire screen
 *
 * @param sb             Target screen buffer.
 * @param row            Current cursor row (for mode 0/1).
 * @param col            Current cursor column (for mode 0/1).
 * @param mode           Erase mode (0, 1, or 2).
 * @param reset_colours  If true, reset colours to default in erased area.
 */
void screen_buf_erase_display(screen_buf_t *sb, int row, int col, int mode,
                              bool reset_colours);

/**
 * @brief Erase parts of a single line (EL – CSI K).
 *
 * Modes:
 *   - 0: cursor → end of line
 *   - 1: start → cursor
 *   - 2: entire line
 *
 * @param sb             Target screen buffer.
 * @param row            Row to erase within.
 * @param col            Current cursor column (for mode 0/1).
 * @param mode           Erase mode (0, 1, or 2).
 * @param reset_colours  If true, reset colours to default in erased area.
 */
void screen_buf_erase_line(screen_buf_t *sb, int row, int col, int mode,
                           bool reset_colours);

#endif // SCREEN_H
