#ifndef SCROLLBACK_H
#define SCROLLBACK_H

// ---------------------------------------------------------------------------
// Scrollback buffer for terminal dicTerm.
//
// A fixed-capacity ring buffer that stores lines that have scrolled off the
// visible terminal screen.  Supports push (line scrolls out), indexed lookup,
// and reset.
// ---------------------------------------------------------------------------

#include <stdint.h>

/** Default number of scrollback lines. */
#define SCROLLBACK_CAPACITY 1000

/**
 * Opaque scrollback handle returned by scrollback_create().
 */
typedef struct scrollback_s scrollback_t;

/**
 * Create a scrollback buffer holding at most `capacity` lines.
 * Each line is `cols` wide.
 * Returns NULL on allocation failure.
 */
scrollback_t *scrollback_create(int capacity, int cols);

/**
 * Destroy a scrollback buffer previously created with scrollback_create().
 * If `sb` is NULL this is a no-op.
 */
void scrollback_destroy(scrollback_t *sb);

/**
 * Push a line (cols bytes) into the scrollback buffer.
 * Characters that are < 0x20 (except tab and newline) are turned into spaces.
 */
void scrollback_push(scrollback_t *sb, const char *line);

/**
 * Return the number of lines currently stored in the scrollback.
 * This is min(capacity, total_pushed).
 */
int scrollback_count(const scrollback_t *sb);

/**
 * Return the total number of lines that have ever been pushed.
 * Useful for knowing absolute scroll distance.
 */
int scrollback_total_pushed(const scrollback_t *sb);

/**
 * Retrieve a stored line by index.
 *
 * @param sb       Scrollback handle.
 * @param index   0 = most recently pushed line,
 *                scrollback_count(sb)-1 = oldest retained line.
 * @param out      Buffer of at least `cols` bytes to receive the line.
 * @param out_len  Size of `out`.
 * @return         1 on success, 0 if index is out of range.
 */
int scrollback_get(const scrollback_t *sb, int index, char *out, int out_len);

/**
 * Clear all stored lines.  Total-pushed counter is NOT reset
 * (so absolute scroll distance remains accurate).
 */
void scrollback_clear(scrollback_t *sb);

/**
 * Reset the scrollback entirely (also resets total-pushed).
 */
void scrollback_reset(scrollback_t *sb);

#endif // SCROLLBACK_H
