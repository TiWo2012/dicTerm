#define _DEFAULT_SOURCE
#include "font.h"
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fontconfig/fontconfig.h>

static bool file_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool str_contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) { h++; n++; }
        if (!*n) return true;
    }
    return false;
}

static bool fc_find(const char *family, char *out, size_t out_sz) {
    FcPattern *pat = FcPatternCreate();
    if (!pat) return false;
    FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)family);
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult result;
    FcPattern *match = FcFontMatch(NULL, pat, &result);
    FcPatternDestroy(pat);
    if (!match) return false;
    /* Fontconfig's FcFontMatch aggressively substitutes when the exact
       family isn't found — it may return a proportional font like Noto
       Sans instead of a monospace one.  Verify we got something usable. */
    FcChar8 *matched_family = NULL;
    FcPatternGetString(match, FC_FAMILY, 0, &matched_family);
    int spacing = 0;
    FcPatternGetInteger(match, FC_SPACING, 0, &spacing);
    bool family_ok = matched_family &&
                     str_contains_ci((const char *)matched_family, family);
    bool mono_ok = spacing == FC_MONO || spacing == FC_DUAL;
    if (!family_ok && !mono_ok) {
        FcPatternDestroy(match);
        return false;
    }
    FcChar8 *file = NULL;
    bool ok = FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch;
    if (ok) {
        strncpy(out, (const char *)file, out_sz - 1);
        out[out_sz - 1] = '\0';
    }
    FcPatternDestroy(match);
    return ok;
}

const char *font_find_nerd_path(void) {
    static char result[FONT_PATH_MAX];
    if (result[0]) return result;
    if (FcInit()) {
        static const char *pref[] = {
            "JetBrainsMonoNerdFont", "FiraCodeNerdFont",
            "AtkynsonMonoNerdFontMono", NULL
        };
        for (int i = 0; pref[i]; i++)
            if (fc_find(pref[i], result, sizeof(result))) { FcFini(); return result; }
        FcFini();
    }
    static const char *const candidates[] = {
        "/usr/share/fonts/OTF/AtkynsonMonoNerdFontMono-Regular.otf",
        "/usr/share/fonts/OTF/AtkynsonMonoNerdFont-Regular.otf",
        "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
        "/usr/share/fonts/TTF/FiraCodeNerdFont-Regular.ttf", NULL
    };
    for (int i = 0; candidates[i]; i++) if (file_exists(candidates[i])) {
        strncpy(result, candidates[i], sizeof(result) - 1); return result;
    }
    return NULL;
}

const char *font_find_regular_path(void) {
    static char result[FONT_PATH_MAX];
    if (result[0]) return result;
    if (FcInit()) {
        static const char *pref[] = {
            "Fira Code", "JetBrains Mono", "Maple Mono",
            "monospace", NULL
        };
        for (int i = 0; pref[i]; i++)
            if (fc_find(pref[i], result, sizeof(result))) { FcFini(); return result; }
        FcFini();
    }
    static const char *const candidates[] = {
        "/usr/share/fonts/MapleMono-TTF/MapleMono-Regular.ttf",
        "/usr/share/fonts/OTF/AtkynsonMono-Regular.otf",
        "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
        "/usr/share/fonts/TTF/FiraCode-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", NULL
    };
    for (int i = 0; candidates[i]; i++) if (file_exists(candidates[i])) {
        strncpy(result, candidates[i], sizeof(result) - 1); return result;
    }
    return NULL;
}

const char *font_find_symbols_path(void) {
    static char result[FONT_PATH_MAX];
    if (result[0]) return result;
    if (FcInit()) {
        if (fc_find("Noto Sans Symbols 2", result, sizeof(result))) { FcFini(); return result; }
        FcFini();
    }
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

bool font_is_nerd(const font_handle_t *handle) { return handle && handle->is_nerd; }
float font_char_width(const font_handle_t *handle) { return handle ? handle->char_width : 10.0f; }
float font_char_height(const font_handle_t *handle) { return handle ? handle->char_height : 20.0f; }
