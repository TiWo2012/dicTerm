#define _DEFAULT_SOURCE
#include "font.h"
#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool file_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

const char *font_find_nerd_path(void) {
    static char result[FONT_PATH_MAX];
    static const char *const candidates[] = {
        "/usr/share/fonts/OTF/AtkynsonMonoNerdFontMono-Regular.otf",
        "/usr/share/fonts/OTF/AtkynsonMonoNerdFont-Regular.otf",
        "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
        "/usr/share/fonts/TTF/FiraCodeNerdFont-Regular.ttf", NULL
    };
    if (result[0]) return result;
    for (int i = 0; candidates[i]; i++) if (file_exists(candidates[i])) {
        strncpy(result, candidates[i], sizeof(result) - 1); return result;
    }
    return NULL;
}

const char *font_find_regular_path(void) {
    static char result[FONT_PATH_MAX];
    static const char *const candidates[] = {
        "/usr/share/fonts/MapleMono-TTF/MapleMono-Regular.ttf",
        "/usr/share/fonts/OTF/AtkynsonMono-Regular.otf",
        "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
        "/usr/share/fonts/TTF/FiraCode-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", NULL
    };
    if (result[0]) return result;
    for (int i = 0; candidates[i]; i++) if (file_exists(candidates[i])) {
        strncpy(result, candidates[i], sizeof(result) - 1); return result;
    }
    return NULL;
}

const char *font_find_symbols_path(void) {
    static const char *const candidates[] = {
        "/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansSymbols-Regular.ttf", NULL
    };
    for (int i = 0; candidates[i]; i++) if (file_exists(candidates[i])) return candidates[i];
    return NULL;
}

int font_build_codepoint_list(int *out, int out_sz, bool include_nerd) {
    if (!out || out_sz <= 0) return 0;
    int count = 0;
    for (int cp = FONT_ASCII_MIN; cp <= FONT_ASCII_MAX && count < out_sz; cp++) out[count++] = cp;
    if (include_nerd) for (int r = 0; r < FONT_NERD_RANGES_COUNT; r++)
        for (int cp = FONT_NERD_RANGES[r][0]; cp <= FONT_NERD_RANGES[r][1] && count < out_sz; cp++) out[count++] = cp;
    return count;
}

font_handle_t *font_init(const char *font_path, const char *nerd_path, float font_size) {
    font_handle_t *handle = calloc(1, sizeof(*handle));
    if (!handle) return NULL;
    const char *regular = font_path && *font_path ? font_path : font_find_regular_path();
    const char *nerd = nerd_path && *nerd_path ? nerd_path : font_find_nerd_path();
    if (!regular) { free(handle); return NULL; }
    strncpy(handle->regular_path, regular, sizeof(handle->regular_path) - 1);
    if (nerd) strncpy(handle->nerd_path, nerd, sizeof(handle->nerd_path) - 1);
    handle->font_size = font_size > 0.0f ? font_size : 20.0f;
    handle->char_width = handle->font_size * 0.6f;
    handle->char_height = handle->font_size;
    handle->ascent = (int)(handle->font_size * 0.8f + 0.999f);
    handle->descent = (int)(handle->font_size * 0.2f + 0.999f);
    handle->initialized = true;
    return handle;
}

void font_uninit(font_handle_t *handle) { free(handle); }

bool font_select(font_handle_t *handle, const char *name) {
    if (!handle || !handle->initialized) return false;
    if (!name || strcmp(name, "regular") == 0) { handle->is_nerd = false; return true; }
    if (strcmp(name, "nerd") == 0 && handle->nerd_path[0]) { handle->is_nerd = true; return true; }
    return false;
}

bool font_is_nerd(const font_handle_t *handle) {
    return handle && handle->is_nerd;
}

const font_glyph_entry_t* font_get_glyph(font_handle_t *handle, int codepoint) {
    if (!handle || !handle->initialized) return NULL;

    font_glyph_entry_t *entry = cache_add_or_update(handle, codepoint);
    if (entry->atlasRec.width == 0 && entry->advanceX <= 0) {
        cache_resolve(handle, entry);
    }
    return entry->valid ? entry : NULL;
}

void font_render_line(font_handle_t *handle, const char *line, int cols,
                      float x, float y, Color fg, Color bg) {
    if (!handle || !handle->initialized || !line || cols <= 0)
        return;

    Font *f = handle->current;
    if (!f || f->texture.id == 0) return;

    int codepoints[512];

    int count = 0;
    int i = 0;
    while (i < cols && count < 512) {
        unsigned char ch = (unsigned char)line[i];
        if (ch < 0x20 && ch != '\t') {
            codepoints[count++] = ' ';
            i++;
        } else if (ch < 0x80) {
            // ASCII
            codepoints[count++] = ch;
            i++;
        } else {
            // Multi-byte UTF-8: decode using raylib
            int codepointSize = 0;
            int cp = GetCodepointNext(&line[i], &codepointSize);
            if (codepointSize <= 0) codepointSize = 1;
            if (cp == 0) cp = ' ';
            codepoints[count++] = cp;
            i += codepointSize;
        }
    }

    if (count == 0) return;

    if (bg.a > 0) {
        float line_width  = (float)count * handle->char_width;
        float line_height = handle->char_height;
        DrawRectangle((int)x, (int)(y),
                      (int)line_width, (int)line_height, bg);
    }

    DrawTextCodepoints(*f, codepoints, count,
                       (Vector2){ x, y + handle->ascent },
                       handle->font_size, handle->spacing, fg);
}

float font_measure_text(font_handle_t *handle, const char *text, int len) {
    if (!handle || !text || len <= 0) return 0;

    int codepoints[512];
    int count = 0;
    int i = 0;
    while (i < len && count < 512) {
        unsigned char ch = (unsigned char)text[i];
        if (ch < 0x20 && ch != '\t') {
            codepoints[count++] = ' ';
            i++;
        } else if (ch < 0x80) {
            codepoints[count++] = ch;
            i++;
        } else {
            int codepointSize = 0;
            int cp = GetCodepointNext(&text[i], &codepointSize);
            if (codepointSize <= 0) codepointSize = 1;
            if (cp == 0) cp = ' ';
            codepoints[count++] = cp;
            i += codepointSize;
        }
    }

    if (count == 0) return 0;

    Font *f = handle->current;
    float scale = (f->baseSize > 0) ? handle->font_size / (float)f->baseSize : 1.0f;
    float total = 0.0f;
    for (int k = 0; k < count; k++) {
        int idx = GetGlyphIndex(*f, codepoints[k]);
        if (f->glyphs[idx].advanceX != 0)
            total += (float)f->glyphs[idx].advanceX * scale;
        else
            total += (f->recs[idx].width + (float)f->glyphs[idx].offsetX) * scale;
        if (k < count - 1)
            total += handle->spacing;
    }
    return total;
}

float font_char_width(const font_handle_t *handle) {
    return handle ? handle->char_width : 10.0f;
}

float font_char_height(const font_handle_t *handle) {
    return handle ? handle->char_height : 20.0f;
}
bool font_is_nerd(const font_handle_t *handle) { return handle && handle->is_nerd; }
float font_char_width(const font_handle_t *handle) { return handle ? handle->char_width : 10.0f; }
float font_char_height(const font_handle_t *handle) { return handle ? handle->char_height : 20.0f; }
