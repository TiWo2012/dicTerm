#ifndef FONT_H
#define FONT_H

#include <stdbool.h>

#define FONT_MAX_CODEPOINTS 8192
#define FONT_PATH_MAX 512
#define FONT_NERD_RANGES_COUNT 4
static const int FONT_NERD_RANGES[FONT_NERD_RANGES_COUNT][2] = {
    {0xE000, 0xEFFF}, {0xF000, 0xF2E0}, {0xF500, 0xFD46}, {0x2500, 0x27BF}
};
#define FONT_ASCII_MIN 0x20
#define FONT_ASCII_MAX 0x7E

typedef struct {
    float font_size;
    float char_width;
    float char_height;
    int ascent;
    int descent;
    bool initialized;
    bool is_nerd;
    char regular_path[FONT_PATH_MAX];
    char nerd_path[FONT_PATH_MAX];
} font_handle_t;

font_handle_t *font_init(const char *font_path, const char *nerd_path,
                         float font_size);
void font_uninit(font_handle_t *handle);
bool font_select(font_handle_t *handle, const char *name);
bool font_is_nerd(const font_handle_t *handle);
float font_char_width(const font_handle_t *handle);
float font_char_height(const font_handle_t *handle);
int font_build_codepoint_list(int *out, int out_sz, bool include_nerd);
const char *font_find_nerd_path(void);
const char *font_find_regular_path(void);
const char *font_find_symbols_path(void);

#endif
