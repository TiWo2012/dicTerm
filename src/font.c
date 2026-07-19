/* dicTerm - Font Rendering Subsystem Implementation */
#define _DEFAULT_SOURCE
#include "font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

// -------------------------------------------------------------------------
//  Internal helpers
// -------------------------------------------------------------------------

/// Check if a path points to a readable regular file.
static bool file_exists(const char *path) {
    if (!path) return false;
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/// Check if a filename has a font extension (.ttf or .otf).
static bool has_font_ext(const char *name) {
    if (!name) return false;
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    return (strcasecmp(ext, ".ttf") == 0 ||
            strcasecmp(ext, ".otf") == 0);
}

// -------------------------------------------------------------------------
//  System font path discovery
// -------------------------------------------------------------------------

/// Common Nerd Fonts installation locations.
static const char *nerd_candidates[] = {
    "/usr/share/fonts/OTF/AtkynsonMonoNerdFontMono-Regular.otf",
    "/usr/share/fonts/OTF/AtkynsonMonoNerdFont-Regular.otf",
    "/usr/share/fonts/TTF/NerdFontsSymbolsOnly-Regular.ttf",
    "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
    "/usr/share/fonts/TTF/FiraCodeNerdFont-Regular.ttf",
    "/usr/share/fonts/NerdFonts/NerdFontsSymbolsOnly-Regular.ttf",
    "/usr/share/fonts/nerd-fonts/NerdFontsSymbolsOnly-Regular.ttf",
    "/usr/share/fonts/nerd-fonts/JetBrainsMonoNerdFont-Regular.ttf",
    "/usr/local/share/fonts/NerdFontsSymbolsOnly-Regular.ttf",
    NULL
};

const char* font_find_nerd_path(void) {
    static char buf[FONT_PATH_MAX] = "";
    if (buf[0] != '\0') return buf;

    for (int i = 0; nerd_candidates[i]; i++) {
        if (file_exists(nerd_candidates[i])) {
            strncpy(buf, nerd_candidates[i], sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            return buf;
        }
    }

    const char *font_dirs[] = {
        "/usr/share/fonts",
        "/usr/local/share/fonts",
        NULL
    };

    for (int d = 0; font_dirs[d]; d++) {
        DIR *dir = opendir(font_dirs[d]);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if ((strstr(entry->d_name, "Nerd") != NULL ||
                     strstr(entry->d_name, "nerd") != NULL) &&
                    has_font_ext(entry->d_name)) {
                    char full[FONT_PATH_MAX];
                    snprintf(full, sizeof(full), "%s/%s", font_dirs[d], entry->d_name);
                    if (file_exists(full)) {
                        closedir(dir);
                        strncpy(buf, full, sizeof(buf) - 1);
                        buf[sizeof(buf) - 1] = '\0';
                        return buf;
                    }
                }
            }
            closedir(dir);
        }

        const char *subdirs[] = { "TTF", "OTF", "truetype", "opentype", NULL };
        for (int s = 0; subdirs[s]; s++) {
            char sub[FONT_PATH_MAX];
            snprintf(sub, sizeof(sub), "%s/%s", font_dirs[d], subdirs[s]);
            dir = opendir(sub);
            if (!dir) continue;

            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if ((strstr(entry->d_name, "Nerd") != NULL ||
                     strstr(entry->d_name, "nerd") != NULL) &&
                    has_font_ext(entry->d_name)) {
                    char full[FONT_PATH_MAX];
                    snprintf(full, sizeof(full), "%s/%s", sub, entry->d_name);
                    if (file_exists(full)) {
                        closedir(dir);
                        strncpy(buf, full, sizeof(buf) - 1);
                        buf[sizeof(buf) - 1] = '\0';
                        return buf;
                    }
                }
            }
            closedir(dir);
        }
    }

    return NULL;
}

/// Common monospace font locations (fallbacks).
static const char *regular_candidates[] = {
    "/usr/share/fonts/OTF/AtkynsonMono-Regular.otf",
    "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
    "/usr/share/fonts/TTF/FiraCode-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/Hack-Regular.ttf",
    "/usr/share/fonts/TTF/UbuntuMono-R.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
    "/usr/share/fonts/truetype/hack/Hack-Regular.ttf",
    NULL
};

const char* font_find_regular_path(void) {
    static char buf[FONT_PATH_MAX] = "";
    if (buf[0] != '\0') return buf;

    for (int i = 0; regular_candidates[i]; i++) {
        if (file_exists(regular_candidates[i])) {
            strncpy(buf, regular_candidates[i], sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            return buf;
        }
    }

    const char *font_dirs[] = {
        "/usr/share/fonts",
        "/usr/local/share/fonts",
        NULL
    };

    const char *mono_keywords[] = {
        "Mono", "mono", "MonoLisa", "Cascadia", "monospace",
        "Terminal", "Console", "DejaVuSans", "Hack", "UbuntuMono",
        NULL
    };

    for (int d = 0; font_dirs[d]; d++) {
        const char *subdirs[] = { "", "/TTF", "/OTF", "/truetype", "/opentype", NULL };
        for (int s = 0; subdirs[s]; s++) {
            char dirpath[FONT_PATH_MAX];
            snprintf(dirpath, sizeof(dirpath), "%s%s", font_dirs[d], subdirs[s]);
            DIR *dir = opendir(dirpath);
            if (!dir) continue;

            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (!has_font_ext(entry->d_name))
                    continue;

                for (int k = 0; mono_keywords[k]; k++) {
                    if (strstr(entry->d_name, mono_keywords[k]) != NULL) {
                        char full[FONT_PATH_MAX];
                        snprintf(full, sizeof(full), "%s/%s", dirpath, entry->d_name);
                        if (file_exists(full)) {
                            closedir(dir);
                            strncpy(buf, full, sizeof(buf) - 1);
                            buf[sizeof(buf) - 1] = '\0';
                            return buf;
                        }
                    }
                }
            }
            closedir(dir);
        }
    }

    return NULL;
}

// -------------------------------------------------------------------------
//  Codepoint list builder
// -------------------------------------------------------------------------

int font_build_codepoint_list(int *out, int out_sz, bool include_nerd) {
    int count = 0;

    // ASCII printable range
    for (int cp = FONT_ASCII_MIN; cp <= FONT_ASCII_MAX && count < out_sz; cp++) {
        out[count++] = cp;
    }

    if (include_nerd) {
        // Nerd Font PUA ranges
        for (int r = 0; r < FONT_NERD_RANGES_COUNT; r++) {
            int start = FONT_NERD_RANGES[r][0];
            int end   = FONT_NERD_RANGES[r][1];
            for (int cp = start; cp <= end && count < out_sz; cp++) {
                out[count++] = cp;
            }
        }
    }

    return count;
}

// -------------------------------------------------------------------------
//  Glyph cache management
// -------------------------------------------------------------------------

static font_glyph_entry_t* cache_lookup(font_handle_t *handle, int codepoint) {
    for (int i = 0; i < handle->glyph_cache_capacity; i++) {
        if (handle->glyph_cache[i].codepoint == codepoint)
            return &handle->glyph_cache[i];
    }
    return NULL;
}

static font_glyph_entry_t* cache_add_or_update(font_handle_t *handle,
                                                int codepoint) {
    font_glyph_entry_t *existing = cache_lookup(handle, codepoint);
    if (existing) return existing;

    for (int i = 0; i < handle->glyph_cache_capacity; i++) {
        if (!handle->glyph_cache[i].valid) {
            handle->glyph_cache[i].codepoint = codepoint;
            handle->glyph_cache[i].valid = true;
            return &handle->glyph_cache[i];
        }
    }

    handle->glyph_cache[0].codepoint = codepoint;
    handle->glyph_cache[0].atlasRec.width = 0;
    handle->glyph_cache[0].advanceX = -1;
    return &handle->glyph_cache[0];
}

static void cache_resolve(font_handle_t *handle, font_glyph_entry_t *entry) {
    Font *f = handle->current;
    if (!f || f->texture.id == 0) return;

    entry->glyphIndex = GetGlyphIndex(*f, entry->codepoint);
    if (entry->glyphIndex < 0) {
        entry->glyphIndex = GetGlyphIndex(*f, '?');
    }

    entry->atlasRec = GetGlyphAtlasRec(*f, entry->codepoint);

    if (entry->glyphIndex >= 0 && entry->glyphIndex < f->glyphCount) {
        entry->advanceX = f->glyphs[entry->glyphIndex].advanceX;
    } else if (entry->atlasRec.width > 0) {
        entry->advanceX = (int)entry->atlasRec.width;
    } else {
        entry->advanceX = (int)(handle->char_width);
    }
}

// -------------------------------------------------------------------------
//  Font loading
// -------------------------------------------------------------------------

static bool load_font_variant(font_handle_t *handle, Font *font_out,
                                const char *path, bool is_nerd) {
    if (!path || !*path) {
        *font_out = GetFontDefault();
        return font_out->texture.id != 0;
    }

    int codepoints[FONT_MAX_CODEPOINTS];
    int count = font_build_codepoint_list(codepoints, FONT_MAX_CODEPOINTS, is_nerd);

    *font_out = LoadFontEx(path, (int)handle->font_size, codepoints, count);

    if (font_out->texture.id == 0) {
        *font_out = GetFontDefault();
    }

    if (font_out->texture.id != 0) {
        SetTextureFilter(font_out->texture, TEXTURE_FILTER_BILINEAR);
    }

    return font_out->texture.id != 0;
}

// -------------------------------------------------------------------------
//  Public API
// -------------------------------------------------------------------------

font_handle_t* font_init(const char *font_path, const char *nerd_path,
                         float font_size) {
    font_handle_t *handle = (font_handle_t*)calloc(1, sizeof(font_handle_t));
    if (!handle) return NULL;

    handle->font_size = (font_size > 0) ? font_size : 20.0f;
    handle->spacing   = 1.0f;

    if (font_path) {
        strncpy(handle->regular_path, font_path, sizeof(handle->regular_path) - 1);
    } else {
        const char *found = font_find_regular_path();
        if (found)
            strncpy(handle->regular_path, found, sizeof(handle->regular_path) - 1);
        else
            handle->regular_path[0] = '\0';
    }

    if (nerd_path) {
        strncpy(handle->nerd_path, nerd_path, sizeof(handle->nerd_path) - 1);
    } else {
        const char *found = font_find_nerd_path();
        if (found)
            strncpy(handle->nerd_path, found, sizeof(handle->nerd_path) - 1);
        else
            handle->nerd_path[0] = '\0';
    }

    handle->glyph_cache_capacity = 256;
    handle->glyph_cache = (font_glyph_entry_t*)calloc(
        handle->glyph_cache_capacity, sizeof(font_glyph_entry_t));
    if (!handle->glyph_cache) {
        free(handle);
        return NULL;
    }

    if (!load_font_variant(handle, &handle->regular,
                           handle->regular_path[0] ? handle->regular_path : NULL,
                           false)) {
        free(handle->glyph_cache);
        free(handle);
        return NULL;
    }

    if (handle->nerd_path[0]) {
        if (!load_font_variant(handle, &handle->nerd,
                                handle->nerd_path, true)) {
            memset(&handle->nerd, 0, sizeof(Font));
        }
    } else {
        memset(&handle->nerd, 0, sizeof(Font));
    }

    handle->current = &handle->regular;
    handle->is_nerd = false;

    // Derive character metrics from the current font
    {
        Font *f = handle->current;
        if (f && f->glyphCount > 0) {
            const char *sample = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            Vector2 sz = MeasureTextEx(*f, sample, handle->font_size, handle->spacing);
            handle->char_width  = sz.x / (float)strlen(sample);
            handle->char_height = sz.y;

            // Compute ascent as the maximum negative offsetY across all glyphs.
            // offsetY is the Y offset from the baseline to the top of the glyph.
            // Ascending glyphs (like 'A', '|') have negative offsetY (glyph goes
            // above the baseline), so -offsetY gives the ascent value.
            // If the font uses a different convention (positive offsetY for all
            // glyphs), -offsetY would be negative, and max_ascent stays at the
            // fallback value.
            int max_ascent = 0;
            int limit = f->glyphCount < 256 ? f->glyphCount : 256;
            for (int i = 0; i < limit; i++) {
                int asc = -f->glyphs[i].offsetY;
                if (asc > max_ascent) max_ascent = asc;
            }
            handle->ascent = max_ascent;
            if (handle->ascent == 0)
                handle->ascent = (int)handle->font_size * 4 / 5;
        } else {
            handle->char_width  = handle->font_size * 0.6f;
            handle->char_height = handle->font_size;
            handle->ascent      = (int)(handle->font_size * 4 / 5);
        }
    }

    handle->initialized = true;
    return handle;
}

void font_uninit(font_handle_t *handle) {
    if (!handle) return;

    if (handle->regular.texture.id != 0)
        UnloadFont(handle->regular);
    if (handle->nerd.texture.id != 0)
        UnloadFont(handle->nerd);

    free(handle->glyph_cache);
    handle->initialized = false;
    free(handle);
}

bool font_select(font_handle_t *handle, const char *name) {
    if (!handle || !handle->initialized) return false;

    if (name == NULL || strcmp(name, "regular") == 0) {
        handle->current = &handle->regular;
        handle->is_nerd = false;
        return true;
    } else if (strcmp(name, "nerd") == 0) {
        if (handle->nerd.texture.id == 0)
            return false;
        handle->current = &handle->nerd;
        handle->is_nerd = true;
        return true;
    }

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
