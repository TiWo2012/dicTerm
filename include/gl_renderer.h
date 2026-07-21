#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include "font.h"
#include "screen.h"
#include <stdbool.h>

typedef struct gl_renderer gl_renderer_t;

gl_renderer_t *gl_renderer_create(const font_handle_t *font);
void gl_renderer_destroy(gl_renderer_t *renderer);
void gl_renderer_begin(gl_renderer_t *renderer, int width, int height);
void gl_renderer_end(gl_renderer_t *renderer);
void gl_renderer_draw_cells(gl_renderer_t *renderer,
                            const screen_cell_t *cells, int count,
                            float x, float y, float cell_width,
                            float cell_height);
void gl_renderer_set_clear_bg(gl_renderer_t *renderer, bool clear_bg);
void gl_renderer_set_bg_opacity(gl_renderer_t *renderer, float opacity);

#endif
