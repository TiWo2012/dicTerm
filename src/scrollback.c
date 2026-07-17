#include "scrollback.h"

#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal structure – ring buffer of fixed-width lines.
// ---------------------------------------------------------------------------

struct scrollback_s {
  int capacity;   // max number of lines
  int cols;       // width of each line
  int head;       // index where the next line will be written
  int count;      // number of valid entries currently stored
  int total;      // total lines ever pushed (monotonic)
  char *buffer;   // flat array: capacity * cols bytes
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

scrollback_t *scrollback_create(int capacity, int cols) {
  if (capacity <= 0 || cols <= 0)
    return NULL;

  scrollback_t *sb = (scrollback_t *)malloc(sizeof(*sb));
  if (!sb)
    return NULL;

  sb->buffer = (char *)malloc((size_t)capacity * (size_t)cols);
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

void scrollback_destroy(scrollback_t *sb) {
  if (!sb)
    return;
  free(sb->buffer);
  free(sb);
}

void scrollback_push(scrollback_t *sb, const char *line) {
  if (!sb || !line)
    return;

  // Copy the line into the current head position.
  memcpy(sb->buffer + (size_t)sb->head * (size_t)sb->cols, line,
         (size_t)sb->cols);

  sb->head = (sb->head + 1) % sb->capacity;
  if (sb->count < sb->capacity)
    sb->count++;
  sb->total++;
}

int scrollback_count(const scrollback_t *sb) {
  return sb ? sb->count : 0;
}

int scrollback_total_pushed(const scrollback_t *sb) {
  return sb ? sb->total : 0;
}

int scrollback_get(const scrollback_t *sb, int index, char *out,
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
  memcpy(out, sb->buffer + (size_t)slot * (size_t)sb->cols, (size_t)copy_len);
  return 1;
}

void scrollback_clear(scrollback_t *sb) {
  if (!sb)
    return;
  sb->head = 0;
  sb->count = 0;
  // Keep total unchanged.
}

void scrollback_reset(scrollback_t *sb) {
  if (!sb)
    return;
  sb->head = 0;
  sb->count = 0;
  sb->total = 0;
}
