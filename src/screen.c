/**
 * @file screen.c
 * @brief Screen buffer implementation.
 *
 * Implements the row-major cell grid with SGR attribute support
 * defined in screen.h.  All functions are bounds-checked and
 * NULL-safe.
 */
#define _DEFAULT_SOURCE
#include "screen.h"
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Screen buffer implementation
// ---------------------------------------------------------------------------

/// @brief Allocate and initialise a screen buffer.
/// @param rows  Number of rows (> 0).
/// @param cols  Number of columns (> 0).
/// @return      New buffer, or NULL on allocation failure.
screen_buf_t* screen_buf_new(int rows, int cols) {
  if (rows <= 0 || cols <= 0) return NULL;

  screen_buf_t *sb = (screen_buf_t*)malloc(sizeof(*sb));
  if (!sb) return NULL;

  sb->cells = (screen_cell_t*)calloc((size_t)rows * (size_t)cols,
                                      sizeof(screen_cell_t));
  if (!sb->cells) {
    free(sb);
    return NULL;
  }

  sb->rows = rows;
  sb->cols = cols;

  // Initialise every cell: space, default light-grey fg, transparent bg.
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      screen_cell_t *cell = &sb->cells[r * cols + c];
      cell->ch = ' ';
      cell->fg[0] = 220; cell->fg[1] = 220; cell->fg[2] = 220;
      cell->bg[0] = 0;   cell->bg[1] = 0;   cell->bg[2] = 0;
      cell->bold = cell->italic = cell->underline = cell->blink = 0;
    }
  }
  return sb;
}

/// @brief Free a screen buffer.
/// @param sb  Buffer to free (NULL-safe).
void screen_buf_free(screen_buf_t *sb) {
  if (!sb) return;
  free(sb->cells);
  free(sb);
}

/// @brief Resize an existing screen buffer to new dimensions.
///
/// Reallocates the cell array and preserves existing content where
/// the old and new grids overlap. New cells are filled with spaces
/// using default colours. If new dimensions equal current dimensions,
/// the function is a no-op.
///
/// @param sb       Buffer to resize.
/// @param new_rows New number of rows (must be > 0).
/// @param new_cols New number of columns (must be > 0).
/// @return         true on success, false on allocation failure.
bool screen_buf_resize(screen_buf_t *sb, int new_rows, int new_cols) {
  if (!sb || new_rows <= 0 || new_cols <= 0) return false;
  if (new_rows == sb->rows && new_cols == sb->cols) return true;

  screen_cell_t *new_cells = (screen_cell_t*)calloc((size_t)new_rows * (size_t)new_cols,
                                                     sizeof(screen_cell_t));
  if (!new_cells) return false;

  // Copy overlapping region from old buffer
  int copy_rows = new_rows < sb->rows ? new_rows : sb->rows;
  int copy_cols = new_cols < sb->cols ? new_cols : sb->cols;
  for (int r = 0; r < copy_rows; r++) {
    size_t src_off = (size_t)r * (size_t)sb->cols;
    size_t dst_off = (size_t)r * (size_t)new_cols;
    memcpy(&new_cells[dst_off], &sb->cells[src_off],
           (size_t)copy_cols * sizeof(screen_cell_t));
  }

  // Initialise new cells (outside copied region) to defaults
  for (int r = 0; r < new_rows; r++) {
    for (int c = 0; c < new_cols; c++) {
      if (r < copy_rows && c < copy_cols) continue; // already copied
      screen_cell_t *cell = &new_cells[(size_t)r * (size_t)new_cols + (size_t)c];
      cell->ch = ' ';
      cell->fg[0] = 220; cell->fg[1] = 220; cell->fg[2] = 220;
      cell->bg[0] = 0;   cell->bg[1] = 0;   cell->bg[2] = 0;
      cell->bold = cell->italic = cell->underline = cell->blink = 0;
    }
  }

  free(sb->cells);
  sb->cells = new_cells;
  sb->rows = new_rows;
  sb->cols = new_cols;
  return true;
}

/// @brief Write a decoded codepoint into a cell without touching colour attributes.
/// @param sb   Target buffer.
/// @param row  Row index.
/// @param col  Column index.
/// @param ch   Unicode codepoint to write.
void screen_buf_put(screen_buf_t *sb, int row, int col, int ch) {
  if (!sb || row < 0 || row >= sb->rows || col < 0 || col >= sb->cols)
    return;
  sb->cells[row * sb->cols + col].ch = ch;
}

/// @brief Return a pointer to a cell, or NULL if out of bounds.
/// @param sb   Target buffer.
/// @param row  Row index.
/// @param col  Column index.
/// @return     Pointer to cell, or NULL.
screen_cell_t* screen_buf_cell(screen_buf_t *sb, int row, int col) {
  if (!sb || row < 0 || row >= sb->rows || col < 0 || col >= sb->cols)
    return NULL;
  return &sb->cells[row * sb->cols + col];
}

/// @brief Apply SGR foreground/background colours to a cell.
/// @param sb   Target buffer.
/// @param row  Row index.
/// @param col  Column index.
/// @param fg   Foreground RGB (3 bytes).
/// @param bg   Background RGB (3 bytes).
void screen_buf_apply_sgr(screen_buf_t *sb, int row, int col,
                          const uint8_t fg[3], const uint8_t bg[3]) {
  screen_cell_t *cell = screen_buf_cell(sb, row, col);
  if (!cell) return;
  cell->fg[0] = fg[0]; cell->fg[1] = fg[1]; cell->fg[2] = fg[2];
  cell->bg[0] = bg[0]; cell->bg[1] = bg[1]; cell->bg[2] = bg[2];
}

/// @brief Clear a single row, optionally resetting colours.
/// @param sb             Target buffer.
/// @param row            Row to clear.
/// @param reset_colours  If true, reset fg/bg and style flags to defaults.
void screen_buf_clear_row(screen_buf_t *sb, int row, bool reset_colours) {
  if (!sb || row < 0 || row >= sb->rows) return;
  for (int c = 0; c < sb->cols; c++) {
    screen_cell_t *cell = &sb->cells[row * sb->cols + c];
    cell->ch = ' ';
    if (reset_colours) {
      cell->fg[0] = 220; cell->fg[1] = 220; cell->fg[2] = 220;
      cell->bg[0] = 0;   cell->bg[1] = 0;   cell->bg[2] = 0;
      cell->bold = cell->italic = cell->underline = cell->blink = 0;
    }
  }
}

/// @brief Erase parts of the display (ED).
///
/// Mode 0: cursor to end of screen.
/// Mode 1: start to cursor.
/// Mode 2: entire screen.
///
/// @param sb             Target buffer.
/// @param row            Cursor row.
/// @param col            Cursor column.
/// @param mode           Erase mode (0, 1, 2).
/// @param reset_colours  If true, reset colours in erased area.
void screen_buf_erase_display(screen_buf_t *sb, int row, int col, int mode,
                              bool reset_colours) {
  if (!sb) return;
  switch (mode) {
  case 0: // cursor → end
    for (int r = row; r < sb->rows; r++) {
      int start = (r == row) ? col : 0;
      for (int c = start; c < sb->cols; c++) {
        screen_cell_t *cell = &sb->cells[r * sb->cols + c];
        cell->ch = ' ';
        if (reset_colours) {
          cell->fg[0] = 220; cell->fg[1] = 220; cell->fg[2] = 220;
          cell->bg[0] = 0;   cell->bg[1] = 0;   cell->bg[2] = 0;
        }
      }
    }
    break;
  case 1: // start → cursor
    for (int r = 0; r <= row; r++) {
      int end = (r == row) ? col : sb->cols - 1;
      for (int c = 0; c <= end; c++) {
        screen_cell_t *cell = &sb->cells[r * sb->cols + c];
        cell->ch = ' ';
        if (reset_colours) {
          cell->fg[0] = 220; cell->fg[1] = 220; cell->fg[2] = 220;
          cell->bg[0] = 0;   cell->bg[1] = 0;   cell->bg[2] = 0;
        }
      }
    }
    break;
  case 2: // entire screen
    for (int r = 0; r < sb->rows; r++)
      screen_buf_clear_row(sb, r, reset_colours);
    break;
  default:
    break;
  }
}

/// @brief Erase parts of a single line (EL).
///
/// Mode 0: cursor to end of line.
/// Mode 1: start to cursor.
/// Mode 2: entire line.
///
/// @param sb             Target buffer.
/// @param row            Row to erase within.
/// @param col            Cursor column.
/// @param mode           Erase mode (0, 1, 2).
/// @param reset_colours  If true, reset colours in erased area.
void screen_buf_erase_line(screen_buf_t *sb, int row, int col, int mode,
                           bool reset_colours) {
  if (!sb || row < 0 || row >= sb->rows) return;
  switch (mode) {
  case 0: // cursor → end
    for (int c = col; c < sb->cols; c++) {
      screen_cell_t *cell = &sb->cells[row * sb->cols + c];
      cell->ch = ' ';
      if (reset_colours) {
        cell->fg[0] = 220; cell->fg[1] = 220; cell->fg[2] = 220;
        cell->bg[0] = 0;   cell->bg[1] = 0;   cell->bg[2] = 0;
      }
    }
    break;
  case 1: // start → cursor
    for (int c = 0; c <= col; c++) {
      screen_cell_t *cell = &sb->cells[row * sb->cols + c];
      cell->ch = ' ';
      if (reset_colours) {
        cell->fg[0] = 220; cell->fg[1] = 220; cell->fg[2] = 220;
        cell->bg[0] = 0;   cell->bg[1] = 0;   cell->bg[2] = 0;
      }
    }
    break;
  case 2: // entire line
    screen_buf_clear_row(sb, row, reset_colours);
    break;
  default:
    break;
  }
}
