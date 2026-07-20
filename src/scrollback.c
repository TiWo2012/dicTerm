/**
 * @file scrollback.c
 * @brief Scrollback ring buffer implementation.
 *
 * Implements a fixed-capacity ring buffer for terminal line history.
 * Each push overwrites the oldest line when at capacity.  Index 0
 * always refers to the most recently pushed line.
 */
#include "scrollback.h"

#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal structure – ring buffer of fixed-width lines.
// ---------------------------------------------------------------------------

/**
 * @brief Ring buffer state.
 *
 * Lines are stored in a flat array of (capacity × cols) integers.
 * head points to the next write slot; the most recent line is at
 * (head − 1) mod capacity.
 */
struct scrollback_s {
  int capacity;   /**< Maximum number of lines the buffer can hold. */
  int cols;       /**< Width of each line in codepoints. */
  int head;       /**< Index where the next line will be written. */
  int count;      /**< Number of valid entries currently stored. */
  int total;      /**< Total lines ever pushed (monotonic, never decreases). */
  int *buffer;    /**< Flat array: capacity * cols codepoints. */
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * @brief Create a scrollback ring buffer.
 *
 * Allocates a buffer of capacity × cols integers and initialises
 * the bookkeeping fields.  The buffer stores lines of codepoints
 * that have scrolled off the visible screen.
 *
 * @param capacity Maximum number of lines to retain (must be > 0).
 * @param cols     Width of each line in codepoints (must be > 0).
 * @return         New scrollback handle, or NULL on allocation failure.
 */
scrollback_t *scrollback_create(int capacity, int cols) {
  if (capacity <= 0 || cols <= 0)
    return NULL;

  scrollback_t *sb = (scrollback_t *)malloc(sizeof(*sb));
  if (!sb)
    return NULL;

  sb->buffer = (int *)malloc((size_t)capacity * (size_t)cols * sizeof(int));
  if (!sb->buffer) {
    free(sb);
    return NULL;
  }

  sb->capacity = capacity;
  sb->cols = cols;
  sb->head = 0;
  sb->count = 0;
  sb->total = 0;
  return sb;
}

/**
 * @brief Destroy a scrollback buffer and free its memory.
 * @param sb  Handle to destroy (NULL-safe).
 */
void scrollback_destroy(scrollback_t *sb) {
  if (!sb)
    return;
  free(sb->buffer);
  free(sb);
}

/**
 * @brief Push a line of codepoints into the scrollback buffer.
 *
 * Non-printable codepoints (< 0x20, except tab) are turned into spaces.
 * If the buffer is at capacity, the oldest line is overwritten.
 *
 * @param sb   Scrollback handle.
 * @param line Array of cols codepoints to store.
 */
void scrollback_push(scrollback_t *sb, const int *line) {
  if (!sb || !line || !sb->buffer)
    return;

  size_t valid_cols = (size_t)sb->cols;
  if (valid_cols == 0) return;
 
  // Copy the line into the current head position.
  memcpy(sb->buffer + (size_t)sb->head * valid_cols, line,
         valid_cols * sizeof(int));

  sb->head = (sb->head + 1) % sb->capacity;
  if ((int)sb->count < sb->capacity)
    sb->count++;
  sb->total++;
}

/**
 * @brief Return the number of lines currently stored in the scrollback.
 * @param sb  Scrollback handle (NULL returns 0).
 * @return    Current count (min(capacity, total_pushed)).
 */
int scrollback_count(const scrollback_t *sb) {
  return sb ? sb->count : 0;
}

/**
 * @brief Return the total number of lines ever pushed (monotonic).
 *
 * Unlike scrollback_count(), this counter is never decreased by
 * clearing.  Useful for calculating absolute scroll distances.
 *
 * @param sb  Scrollback handle (NULL returns 0).
 * @return    Total pushes since creation or last reset.
 */
int scrollback_total_pushed(const scrollback_t *sb) {
  return sb ? sb->total : 0;
}

/**
 * @brief Retrieve a stored line by index.
 *
 * Index 0 = most recently pushed line.
 * Index scrollback_count(sb)-1 = oldest retained line.
 *
 * @param sb      Scrollback handle.
 * @param index   0-based index (0 = most recent).
 * @param out     Output buffer (must hold at least out_len codepoints).
 * @param out_len Size of out in codepoints.
 * @return        1 on success, 0 if index is out of range.
 */
int scrollback_get(const scrollback_t *sb, int index, int *out,
                   int out_len) {
  if (!sb || !out || index < 0 || index >= sb->count)
    return 0;

  // Index 0 = most recent.  The most recent line is stored at
  // (head - 1) modulo capacity.
  int slot = (sb->head - 1 - index) % sb->capacity;
  if (slot < 0)
    slot += sb->capacity;

  int copy_len = sb->cols;
  if (copy_len > out_len)
    copy_len = out_len;
  memcpy(out, sb->buffer + (size_t)slot * (size_t)sb->cols,
         (size_t)copy_len * sizeof(int));
  return 1;
}

/**
 * @brief Clear all stored lines.
 *
 * The total-pushed counter is NOT reset (absolute scroll distance
 * remains accurate).  Lines already retrieved remain valid.
 *
 * @param sb  Scrollback handle (NULL-safe).
 */
void scrollback_clear(scrollback_t *sb) {
  if (!sb)
    return;
  sb->head = 0;
  sb->count = 0;
  // Keep total unchanged.
}

/**
 * @brief Fully reset the scrollback buffer (including total-pushed counter).
 *
 * Unlike scrollback_clear(), this also resets the monotonic
 * total-pushed counter to zero.
 *
 * @param sb  Scrollback handle (NULL-safe).
 */
void scrollback_reset(scrollback_t *sb) {
  if (!sb)
    return;
  sb->head = 0;
  sb->count = 0;
  sb->total = 0;
}
