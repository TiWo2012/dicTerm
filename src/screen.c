#define _DEFAULT_SOURCE
#include "screen.h"
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Screen buffer implementation
// ---------------------------------------------------------------------------

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

void screen_buf_free(screen_buf_t *sb) {
  if (!sb) return;
  free(sb->cells);
  free(sb);
}

void screen_buf_put(screen_buf_t *sb, int row, int col, uint8_t ch) {
  if (!sb || row < 0 || row >= sb->rows || col < 0 || col >= sb->cols)
    return;
  sb->cells[row * sb->cols + col].ch = ch;
}

screen_cell_t* screen_buf_cell(screen_buf_t *sb, int row, int col) {
  if (!sb || row < 0 || row >= sb->rows || col < 0 || col >= sb->cols)
    return NULL;
  return &sb->cells[row * sb->cols + col];
}

void screen_buf_apply_sgr(screen_buf_t *sb, int row, int col,
                          const uint8_t fg[3], const uint8_t bg[3]) {
  screen_cell_t *cell = screen_buf_cell(sb, row, col);
  if (!cell) return;
  cell->fg[0] = fg[0]; cell->fg[1] = fg[1]; cell->fg[2] = fg[2];
  cell->bg[0] = bg[0]; cell->bg[1] = bg[1]; cell->bg[2] = bg[2];
}

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
  }
}

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
  }
}
