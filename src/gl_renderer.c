#define _DEFAULT_SOURCE
#include "gl_renderer.h"

#include <GL/glcorearb.h>
#include <GLFW/glfw3.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#pragma clang diagnostic pop
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    PFNGLCREATESHADERPROC create_shader;
    PFNGLSHADERSOURCEPROC shader_source;
    PFNGLCOMPILESHADERPROC compile_shader;
    PFNGLGETSHADERIVPROC get_shader_iv;
    PFNGLGETSHADERINFOLOGPROC get_shader_log;
    PFNGLCREATEPROGRAMPROC create_program;
    PFNGLATTACHSHADERPROC attach_shader;
    PFNGLLINKPROGRAMPROC link_program;
    PFNGLGETPROGRAMIVPROC get_program_iv;
    PFNGLDELETEPROGRAMPROC delete_program;
    PFNGLDELETESHADERPROC delete_shader;
    PFNGLUSEPROGRAMPROC use_program;
    PFNGLGETUNIFORMLOCATIONPROC get_uniform;
    PFNGLUNIFORM2FPROC uniform2f;
    PFNGLUNIFORM4FPROC uniform4f;
    PFNGLUNIFORM1IPROC uniform1i;
    PFNGLGENVERTEXARRAYSPROC gen_vertex_arrays;
    PFNGLBINDVERTEXARRAYPROC bind_vertex_array;
    PFNGLDELETEVERTEXARRAYSPROC delete_vertex_arrays;
    PFNGLGENBUFFERSPROC gen_buffers;
    PFNGLBINDBUFFERPROC bind_buffer;
    PFNGLBUFFERDATAPROC buffer_data;
    PFNGLDELETEBUFFERSPROC delete_buffers;
    PFNGLENABLEVERTEXATTRIBARRAYPROC enable_attrib;
    PFNGLVERTEXATTRIBPOINTERPROC attrib_pointer;
    PFNGLDRAWARRAYSPROC draw_arrays;
    PFNGLENABLEPROC enable;
    PFNGLBLENDFUNCPROC blend_func;
    PFNGLGENTEXTURESPROC gen_textures;
    void (*bind_texture)(GLenum target, GLuint texture);
    PFNGLTEXIMAGE2DPROC tex_image_2d;
    PFNGLTEXPARAMETERIPROC tex_parameter_i;
    PFNGLTEXSUBIMAGE2DPROC tex_sub_image_2d;
    PFNGLPIXELSTOREIPROC pixel_store_i;
    PFNGLDELETETEXTURESPROC delete_textures;
    void (*active_texture)(GLenum texture);
} gl_api_t;

typedef struct {
    unsigned int face_id;
    unsigned int id;
    int advance;
    int bearing_x;
    int bearing_y;
    int width;
    int height;
    float u0;
    float v0;
    float u1;
    float v1;
} glyph_t;

struct gl_renderer {
    FT_Library ft;
    FT_Face face;
    FT_Face nerd_face;
    hb_font_t *hb_font;
    hb_buffer_t *buffer;
    gl_api_t gl;
    unsigned int program;
    unsigned int vao;
    unsigned int vbo;
    unsigned int texture;
    int atlas_x;
    int atlas_y;
    int atlas_row;
    int atlas_size;
    int width;
    int height;
    int uniform_screen;
    int uniform_color;
    int uniform_use_atlas;
    int uniform_atlas;
    glyph_t *glyphs;
    int glyph_count;
    int glyph_capacity;
    bool clear_bg;
    float bg_opacity;
};

static void *proc(const char *name) {
    return (void *)glfwGetProcAddress(name);
}

static bool load_api(gl_api_t *g) {
#define LOAD(field, type, name) do { g->field = (type)proc(name); if (!g->field) return false; } while (0)
    LOAD(create_shader, PFNGLCREATESHADERPROC, "glCreateShader");
    LOAD(shader_source, PFNGLSHADERSOURCEPROC, "glShaderSource");
    LOAD(compile_shader, PFNGLCOMPILESHADERPROC, "glCompileShader");
    LOAD(get_shader_iv, PFNGLGETSHADERIVPROC, "glGetShaderiv");
    LOAD(get_shader_log, PFNGLGETSHADERINFOLOGPROC, "glGetShaderInfoLog");
    LOAD(create_program, PFNGLCREATEPROGRAMPROC, "glCreateProgram");
    LOAD(attach_shader, PFNGLATTACHSHADERPROC, "glAttachShader");
    LOAD(link_program, PFNGLLINKPROGRAMPROC, "glLinkProgram");
    LOAD(get_program_iv, PFNGLGETPROGRAMIVPROC, "glGetProgramiv");
    LOAD(delete_program, PFNGLDELETEPROGRAMPROC, "glDeleteProgram");
    LOAD(delete_shader, PFNGLDELETESHADERPROC, "glDeleteShader");
    LOAD(use_program, PFNGLUSEPROGRAMPROC, "glUseProgram");
    LOAD(get_uniform, PFNGLGETUNIFORMLOCATIONPROC, "glGetUniformLocation");
    LOAD(uniform2f, PFNGLUNIFORM2FPROC, "glUniform2f");
    LOAD(uniform4f, PFNGLUNIFORM4FPROC, "glUniform4f");
    LOAD(uniform1i, PFNGLUNIFORM1IPROC, "glUniform1i");
    LOAD(gen_vertex_arrays, PFNGLGENVERTEXARRAYSPROC, "glGenVertexArrays");
    LOAD(bind_vertex_array, PFNGLBINDVERTEXARRAYPROC, "glBindVertexArray");
    LOAD(delete_vertex_arrays, PFNGLDELETEVERTEXARRAYSPROC, "glDeleteVertexArrays");
    LOAD(gen_buffers, PFNGLGENBUFFERSPROC, "glGenBuffers");
    LOAD(bind_buffer, PFNGLBINDBUFFERPROC, "glBindBuffer");
    LOAD(buffer_data, PFNGLBUFFERDATAPROC, "glBufferData");
    LOAD(delete_buffers, PFNGLDELETEBUFFERSPROC, "glDeleteBuffers");
    LOAD(enable_attrib, PFNGLENABLEVERTEXATTRIBARRAYPROC, "glEnableVertexAttribArray");
    LOAD(attrib_pointer, PFNGLVERTEXATTRIBPOINTERPROC, "glVertexAttribPointer");
    LOAD(draw_arrays, PFNGLDRAWARRAYSPROC, "glDrawArrays");
    LOAD(enable, PFNGLENABLEPROC, "glEnable");
    LOAD(blend_func, PFNGLBLENDFUNCPROC, "glBlendFunc");
    LOAD(gen_textures, PFNGLGENTEXTURESPROC, "glGenTextures");
    g->bind_texture = (void (*)(GLenum, GLuint))proc("glBindTexture"); if (!g->bind_texture) return false;
    LOAD(tex_image_2d, PFNGLTEXIMAGE2DPROC, "glTexImage2D");
    LOAD(tex_parameter_i, PFNGLTEXPARAMETERIPROC, "glTexParameteri");
    LOAD(tex_sub_image_2d, PFNGLTEXSUBIMAGE2DPROC, "glTexSubImage2D");
    LOAD(pixel_store_i, PFNGLPIXELSTOREIPROC, "glPixelStorei");
    LOAD(delete_textures, PFNGLDELETETEXTURESPROC, "glDeleteTextures");
    LOAD(active_texture, PFNGLACTIVETEXTUREPROC, "glActiveTexture");
#undef LOAD
    return true;
}

static unsigned int make_shader(gl_renderer_t *r, GLenum type, const char *source) {
    unsigned int shader = r->gl.create_shader(type);
    r->gl.shader_source(shader, 1, &source, NULL);
    r->gl.compile_shader(shader);
    GLint ok = 0;
    r->gl.get_shader_iv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) return shader;
    r->gl.delete_shader(shader);
    return 0U;
}

static bool make_program(gl_renderer_t *r) {
    static const char *vertex = "#version 330 core\nlayout(location=0) in vec2 p; layout(location=1) in vec2 uv; uniform vec2 screen; out vec2 tex; void main(){ vec2 q=p/screen*2.0-1.0; gl_Position=vec4(q.x,-q.y,0,1); tex=uv; }";
    static const char *fragment = "#version 330 core\nin vec2 tex; uniform sampler2D atlas; uniform vec4 color; uniform int use_atlas; out vec4 out_color; void main(){ float coverage = use_atlas != 0 ? texture(atlas,tex).r : 1.0; out_color=vec4(color.rgb,color.a*coverage); }";
    unsigned int vs = make_shader(r, GL_VERTEX_SHADER, vertex);
    unsigned int fs = make_shader(r, GL_FRAGMENT_SHADER, fragment);
    if (!vs || !fs) {
        if (vs) r->gl.delete_shader(vs);
        if (fs) r->gl.delete_shader(fs);
        return false;
    }
    r->program = r->gl.create_program();
    r->gl.attach_shader(r->program, vs); r->gl.attach_shader(r->program, fs);
    r->gl.link_program(r->program);
    GLint ok = 0; r->gl.get_program_iv(r->program, GL_LINK_STATUS, &ok);
    r->gl.delete_shader(vs); r->gl.delete_shader(fs);
    if (!ok) { r->gl.delete_program(r->program); r->program = 0; return false; }
    r->uniform_screen = r->gl.get_uniform(r->program, "screen");
    r->uniform_color = r->gl.get_uniform(r->program, "color");
    r->uniform_use_atlas = r->gl.get_uniform(r->program, "use_atlas");
    r->uniform_atlas = r->gl.get_uniform(r->program, "atlas");
    return true;
}

gl_renderer_t *gl_renderer_create(const font_handle_t *font) {
    if (!font || !font->regular_path[0]) return NULL;
    gl_renderer_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    if (FT_Init_FreeType(&r->ft) != 0 || FT_New_Face(r->ft, font->regular_path, 0, &r->face) != 0) {
        gl_renderer_destroy(r);
        return NULL;
    }
    FT_Set_Pixel_Sizes(r->face, 0, (unsigned int)font->font_size);
    if (font->nerd_path[0] &&
        FT_New_Face(r->ft, font->nerd_path, 0, &r->nerd_face) == 0) {
        FT_Set_Pixel_Sizes(r->nerd_face, 0, (unsigned int)font->font_size);
    }
    r->hb_font = hb_ft_font_create_referenced(r->face);
    r->buffer = hb_buffer_create();
    r->atlas_size = 2048;
    if (!r->hb_font || !r->buffer || !load_api(&r->gl) || !make_program(r)) { gl_renderer_destroy(r); return NULL; }
    r->gl.gen_textures(1, &r->texture); r->gl.bind_texture(GL_TEXTURE_2D, r->texture);
    size_t atlas_bytes = (size_t)r->atlas_size * (size_t)r->atlas_size;
    unsigned char *empty_atlas = calloc(atlas_bytes, 1);
    if (!empty_atlas) { gl_renderer_destroy(r); return NULL; }
    r->gl.tex_image_2d(GL_TEXTURE_2D, 0, GL_RED, r->atlas_size, r->atlas_size,
                        0, GL_RED, GL_UNSIGNED_BYTE, empty_atlas);
    free(empty_atlas);
    r->gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    r->gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    r->gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    r->gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    r->gl.gen_vertex_arrays(1, &r->vao); r->gl.gen_buffers(1, &r->vbo);
    r->gl.bind_vertex_array(r->vao); r->gl.bind_buffer(GL_ARRAY_BUFFER, r->vbo);
    r->gl.enable_attrib(0); r->gl.attrib_pointer(0, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), (void *)0);
    r->gl.enable_attrib(1); r->gl.attrib_pointer(1, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), (void *)(2 * sizeof(float)));
    return r;
}

void gl_renderer_destroy(gl_renderer_t *r) {
    if (!r) return;
    if (r->hb_font) hb_font_destroy(r->hb_font); if (r->buffer) hb_buffer_destroy(r->buffer);
    if (r->nerd_face) FT_Done_Face(r->nerd_face);
    if (r->face) FT_Done_Face(r->face); if (r->ft) FT_Done_FreeType(r->ft);
    if (r->program && r->gl.delete_program) r->gl.delete_program(r->program);
    if (r->vbo && r->gl.delete_buffers) r->gl.delete_buffers(1, &r->vbo);
    if (r->vao && r->gl.delete_vertex_arrays) r->gl.delete_vertex_arrays(1, &r->vao);
    if (r->texture && r->gl.delete_textures) r->gl.delete_textures(1, &r->texture);
    free(r->glyphs);
    free(r);
}

void gl_renderer_begin(gl_renderer_t *r, int width, int height) {
    if (!r || width <= 0 || height <= 0) return;
    r->width = width; r->height = height;
    r->gl.use_program(r->program); r->gl.uniform2f(r->uniform_screen, (float)width, (float)height);
    r->gl.uniform1i(r->uniform_atlas, 0);
    r->gl.active_texture(GL_TEXTURE0); r->gl.bind_texture(GL_TEXTURE_2D, r->texture); r->gl.bind_vertex_array(r->vao);
    r->gl.enable(GL_BLEND);
    r->gl.blend_func(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void gl_renderer_end(gl_renderer_t *r) { if (r) r->gl.use_program(0); }

void gl_renderer_set_clear_bg(gl_renderer_t *r, bool clear_bg) {
    if (r) r->clear_bg = clear_bg;
}

void gl_renderer_set_bg_opacity(gl_renderer_t *r, float opacity) {
    if (r) r->bg_opacity = opacity;
}

static bool same_style(const screen_cell_t *a, const screen_cell_t *b) {
    return a->fg[0] == b->fg[0] && a->fg[1] == b->fg[1] &&
           a->fg[2] == b->fg[2] && a->bg[0] == b->bg[0] &&
           a->bg[1] == b->bg[1] && a->bg[2] == b->bg[2] &&
           a->bold == b->bold && a->underline == b->underline;
}

static glyph_t *get_glyph(gl_renderer_t *r, FT_Face face, unsigned int face_id,
                          unsigned int id) {
    for (int i = 0; i < r->glyph_count; i++)
        if (r->glyphs[i].face_id == face_id && r->glyphs[i].id == id)
            return &r->glyphs[i];
    if (FT_Load_Glyph(face, id, FT_LOAD_DEFAULT) != 0) return NULL;
    FT_GlyphSlot slot = face->glyph; if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) return NULL;
    if (r->glyph_count == r->glyph_capacity) {
        int capacity = r->glyph_capacity ? r->glyph_capacity * 2 : 512;
        glyph_t *glyphs = realloc(r->glyphs, (size_t)capacity * sizeof(*glyphs));
        if (!glyphs) return NULL;
        r->glyphs = glyphs;
        r->glyph_capacity = capacity;
    }
    if (r->atlas_x + (int)slot->bitmap.width + 2 >= r->atlas_size) { r->atlas_x = 0; r->atlas_y += r->atlas_row + 2; r->atlas_row = 0; }
    if (r->atlas_y + (int)slot->bitmap.rows >= r->atlas_size) return NULL;
    size_t row_bytes = (size_t)slot->bitmap.width;
    size_t bitmap_bytes = row_bytes * (size_t)slot->bitmap.rows;
    unsigned char *packed = bitmap_bytes ? malloc(bitmap_bytes) : NULL;
    if (bitmap_bytes && !packed) return NULL;
    int pitch = slot->bitmap.pitch;
    for (unsigned int row = 0; row < slot->bitmap.rows; row++) {
        const unsigned char *source;
        if (pitch >= 0)
            source = slot->bitmap.buffer + (size_t)row * (size_t)pitch;
        else
            source = slot->bitmap.buffer + (size_t)(slot->bitmap.rows - 1U - row) * (size_t)(-pitch);
        memcpy(packed + (size_t)row * row_bytes, source, row_bytes);
    }
    // raylib may leave non-default unpack state active.  In particular, a
    // non-zero row length makes otherwise-correct glyph rows appear as
    // diagonal streaks when uploaded into the atlas.
    if (bitmap_bytes) {
        r->gl.pixel_store_i(GL_UNPACK_ALIGNMENT, 1);
        r->gl.pixel_store_i(GL_UNPACK_ROW_LENGTH, 0);
        r->gl.pixel_store_i(GL_UNPACK_SKIP_ROWS, 0);
        r->gl.pixel_store_i(GL_UNPACK_SKIP_PIXELS, 0);
        r->gl.tex_sub_image_2d(GL_TEXTURE_2D, 0, r->atlas_x, r->atlas_y,
                                (GLsizei)slot->bitmap.width,
                                (GLsizei)slot->bitmap.rows, GL_RED,
                                GL_UNSIGNED_BYTE, packed);
    }
    free(packed);
    glyph_t *g = &r->glyphs[r->glyph_count++]; *g = (glyph_t){ face_id, id, (int)(slot->advance.x >> 6), slot->bitmap_left, slot->bitmap_top, (int)slot->bitmap.width, (int)slot->bitmap.rows, (float)r->atlas_x / (float)r->atlas_size, (float)r->atlas_y / (float)r->atlas_size, (float)(r->atlas_x + (int)slot->bitmap.width) / (float)r->atlas_size, (float)(r->atlas_y + (int)slot->bitmap.rows) / (float)r->atlas_size };
    r->atlas_x += (int)slot->bitmap.width + 2; if ((int)slot->bitmap.rows > r->atlas_row) r->atlas_row = (int)slot->bitmap.rows; return g;
}

static void draw_rect(gl_renderer_t *r, float x, float y, float width,
                      float height, const uint8_t color[3], float alpha) {
    const float vertices[] = {
        x, y, 0.0f, 0.0f, x + width, y, 1.0f, 0.0f,
        x + width, y + height, 1.0f, 1.0f, x, y, 0.0f, 0.0f,
        x + width, y + height, 1.0f, 1.0f, x, y + height, 0.0f, 1.0f
    };
    r->gl.uniform1i(r->uniform_use_atlas, 0);
    r->gl.uniform4f(r->uniform_color, (float)color[0] / 255.0f,
                    (float)color[1] / 255.0f, (float)color[2] / 255.0f, alpha);
    r->gl.bind_buffer(GL_ARRAY_BUFFER, r->vbo);
    r->gl.buffer_data(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(vertices), vertices, GL_STREAM_DRAW);
    r->gl.draw_arrays(GL_TRIANGLES, 0, 6);
}

void gl_renderer_draw_cells(gl_renderer_t *r, const screen_cell_t *cells, int count, float x, float y, float cell_width, float cell_height) {
    if (!r || !cells || count <= 0) return;
    /* Shape contiguous runs with identical SGR colour/style.  A terminal
       row is not necessarily one shaping run: changing colour in the
       middle must not make the first cell's colour bleed across the row. */
    for (int i = 1; i < count; i++) {
        if (same_style(cells + i, cells)) continue;
        int start = 0;
        while (start < count) {
            int end = start + 1;
            while (end < count && cells[end].fg[0] == cells[start].fg[0] &&
                   cells[end].fg[1] == cells[start].fg[1] &&
                   cells[end].fg[2] == cells[start].fg[2] &&
                   cells[end].bg[0] == cells[start].bg[0] &&
                   cells[end].bg[1] == cells[start].bg[1] &&
                   cells[end].bg[2] == cells[start].bg[2] &&
                   cells[end].bold == cells[start].bold &&
                   cells[end].underline == cells[start].underline) end++;
            gl_renderer_draw_cells(r, cells + start, end - start,
                                   x + (float)start * cell_width, y,
                                   cell_width, cell_height);
            start = end;
        }
        return;
    }
    for (int i = 0; i < count; i++) {
        float alpha = 1.0f;
        if (r->clear_bg && cells[i].bg[0] == 0 && cells[i].bg[1] == 0 && cells[i].bg[2] == 0)
            alpha = r->bg_opacity;
        draw_rect(r, x + (float)i * cell_width, y, cell_width, cell_height, cells[i].bg, alpha);
    }
    hb_codepoint_t *text = malloc((size_t)count * sizeof(*text)); if (!text) return;
    for (int i = 0; i < count; i++) text[i] = (hb_codepoint_t)(cells[i].ch < 0x20 ? ' ' : cells[i].ch);
    hb_buffer_clear_contents(r->buffer); hb_buffer_add_codepoints(r->buffer, text, count, 0, count); hb_buffer_set_direction(r->buffer, HB_DIRECTION_LTR); hb_buffer_set_script(r->buffer, HB_SCRIPT_LATIN); hb_buffer_set_language(r->buffer, hb_language_from_string("en", -1)); hb_feature_t features[] = {{HB_TAG('l','i','g','a'), 1, 0, (unsigned int)-1},{HB_TAG('c','l','i','g'), 1, 0, (unsigned int)-1}}; hb_shape(r->hb_font, r->buffer, features, 2);
    unsigned int n = 0; hb_glyph_info_t *info = hb_buffer_get_glyph_infos(r->buffer, &n); hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(r->buffer, &n);
    float cr = (float)cells[0].fg[0] / 255.0f, cg = (float)cells[0].fg[1] / 255.0f, cb = (float)cells[0].fg[2] / 255.0f;
    r->gl.uniform4f(r->uniform_color, cr, cg, cb, 1.0f);
    r->gl.uniform1i(r->uniform_use_atlas, 1);
    float pen_x = x;
    for (unsigned int i = 0; i < n; i++) {
        FT_Face face = r->face;
        unsigned int face_id = 0;
        unsigned int glyph_id = info[i].codepoint;
        unsigned int cluster = info[i].cluster;
        unsigned int next_cluster = (i + 1U < n) ? info[i + 1U].cluster : (unsigned int)count;
        int span = (int)(next_cluster - cluster);
        if (r->nerd_face && cluster < (unsigned int)count &&
            FT_Get_Char_Index(r->face, (FT_ULong)text[cluster]) == 0) {
            unsigned int nerd_glyph = FT_Get_Char_Index(r->nerd_face,
                                                          (FT_ULong)text[cluster]);
            if (nerd_glyph) {
                face = r->nerd_face;
                face_id = 1;
                glyph_id = nerd_glyph;
            }
        }
        glyph_t *g = get_glyph(r, face, face_id, glyph_id);
        if (!g) {
            if (span > 1) pen_x += (float)pos[i].x_advance / 64.0f;
            continue;
        }
        float gx;
        if (span > 1) {
            /* Ligature spanning multiple cells: position from the HarfBuzz
               pen to let the glyph's natural advance define its extent. */
            gx = pen_x + (float)pos[i].x_offset / 64.0f + (float)g->bearing_x;
            pen_x += (float)pos[i].x_advance / 64.0f;
        } else {
            /* Single-cell glyph: anchor to the cell grid to prevent drift
               when the configured cell width and font advance differ. */
            gx = x + (float)cluster * cell_width +
                 (float)pos[i].x_offset / 64.0f + (float)g->bearing_x;
            pen_x = x + (float)(cluster + 1) * cell_width;
        }
        float gy = y + cell_height - (float)g->bearing_y;
        float w = (float)g->width, h = (float)g->height;
        float v[] = {gx, gy, g->u0, g->v0, gx + w, gy, g->u1, g->v0,
                     gx + w, gy + h, g->u1, g->v1, gx, gy, g->u0, g->v0,
                     gx + w, gy + h, g->u1, g->v1, gx, gy + h, g->u0, g->v1};
        r->gl.bind_buffer(GL_ARRAY_BUFFER, r->vbo);
        r->gl.buffer_data(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(v), v, GL_STREAM_DRAW);
        r->gl.draw_arrays(GL_TRIANGLES, 0, 6);
        if (cells[0].bold) {
            for (int j = 0; j < 24; j += 4) v[j] += 0.75f;
            r->gl.buffer_data(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(v), v, GL_STREAM_DRAW);
            r->gl.draw_arrays(GL_TRIANGLES, 0, 6);
        }
    }
    if (cells[0].underline)
        draw_rect(r, x, y + cell_height - 2.0f, (float)count * cell_width, 1.0f, cells[0].fg, 1.0f);
    free(text);
}
