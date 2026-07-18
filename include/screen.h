#ifndef SCREEN_H
#define SCREEN_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Screen cell with full SGR (Select Graphic Rendition) attributes.
//
// Each cell in the terminal grid stores:
//   - The character byte (7-bit ASCII or UTF-8 leader)
//   - Foreground colour (RGB, 0 = default/reset)
//   - Background colour (RGB, 0 = transparent/default)
//   - Bold, italic, underline flags (for future use)
// ---------------------------------------------------------------------------

typedef struct {
  uint8_t ch;               // character byte
  uint8_t fg[3];            // RGB foreground colour (0 = default)
  uint8_t bg[3];            // RGB background colour (0 = transparent)
  uint8_t bold : 1;
  uint8_t italic : 1;
  uint8_t underline : 1;
  uint8_t blink : 1;
} screen_cell_t;

// Helper to read colour bytes (avoids strict-aliasing warnings).
static inline uint32_t screen_cell_fg_u32(const screen_cell_t *c) {
  return ((uint32_t)c->fg[0] << 16) |
         ((uint32_t)c->fg[1] <<  8) |
         ((uint32_t)c->fg[2]);
}

static inline uint32_t screen_cell_bg_u32(const screen_cell_t *c) {
  return ((uint32_t)c->bg[0] << 16) |
         ((uint32_t)c->bg[1] <<  8) |
         ((uint32_t)c->bg[2]);
}

// ---------------------------------------------------------------------------
// Screen buffer – a simple rows×cols grid of cells.
// ---------------------------------------------------------------------------

typedef struct {
  screen_cell_t *cells;     // flat row-major array: [row * cols + col]
  int rows;
  int cols;
} screen_buf_t;

/**
 * Allocate and initialise a screen buffer.
 * Every cell is set to space (0x20) with default fg = {220,220,220}
 * and bg = {0,0,0} (transparent).
 * Returns NULL on allocation failure.
 */
screen_buf_t* screen_buf_new(int rows, int cols);

/**
 * Free a screen buffer previously created with screen_buf_new().
 */
void screen_buf_free(screen_buf_t *sb);

/**
 * Write one character into a cell.
 * The cell's colour attributes are NOT touched (preserves SGR state).
 */
void screen_buf_put(screen_buf_t *sb, int row, int col, uint8_t ch);

/**
 * Return a pointer to the cell at (row, col).
 * Bounds-checked; returns NULL if row/col are out of range.
 */
screen_cell_t* screen_buf_cell(screen_buf_t *sb, int row, int col);

/**
 * Apply SGR colour to the "current" cell (row, col).
 * This sets fg and bg and is typically called after a CSI m sequence.
 */
void screen_buf_apply_sgr(screen_buf_t *sb, int row, int col,
                          const uint8_t fg[3], const uint8_t bg[3]);

/**
 * Fill an entire row with spaces, preserving existing colours
 * (or resetting them if `reset_colours` is true).
 */
void screen_buf_clear_row(screen_buf_t *sb, int row, bool reset_colours);

/**
 * Erase parts of the display:
 *   0 – cursor to end of screen
 *   1 – start to cursor
 *   2 – entire screen
 * Reset colours to default if `reset_colours` is true.
 */
void screen_buf_erase_display(screen_buf_t *sb, int row, int col, int mode,
                              bool reset_colours);

/**
 * Erase parts of the current line:
 *   0 – cursor to end of line
 *   1 – start to cursor
 *   2 – entire line
 */
void screen_buf_erase_line(screen_buf_t *sb, int row, int col, int mode,
                           bool reset_colours);

#endif // SCREEN_H
