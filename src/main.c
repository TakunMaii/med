#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-c.h>

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MED_SHADER_DIR
#define MED_SHADER_DIR "."
#endif

#ifndef MED_DEFAULT_FONT
#define MED_DEFAULT_FONT "/usr/share/fonts/myfonts/CaskaydiaCove/CaskaydiaCoveNerdFontMono-Regular.ttf"
#endif

#define MED_FONT_SIZE 14
#define MAX_FRAMES 2
#define ATLAS_W 1024
#define ATLAS_H 1024
#define MAX_VERTICES 262144

typedef struct {
    float r, g, b, a;
} Color;

typedef struct {
    Color bg, fg, gutter_bg, gutter_fg, line_no_current;
    Color cursor, cursor_insert, selection;
    Color keyword, string, comment, function, type, number, preproc;
} Theme;

static const Theme gruvbox = {
    .bg = {0.157f, 0.157f, 0.157f, 1.0f},
    .fg = {0.922f, 0.859f, 0.698f, 1.0f},
    .gutter_bg = {0.157f, 0.157f, 0.157f, 1.0f},
    .gutter_fg = {0.573f, 0.514f, 0.455f, 1.0f},
    .line_no_current = {0.980f, 0.741f, 0.184f, 1.0f},
    .cursor = {0.922f, 0.859f, 0.698f, 0.38f},
    .cursor_insert = {0.980f, 0.741f, 0.184f, 1.0f},
    .selection = {0.271f, 0.522f, 0.533f, 0.45f},
    .keyword = {0.984f, 0.286f, 0.204f, 1.0f},
    .string = {0.722f, 0.733f, 0.149f, 1.0f},
    .comment = {0.573f, 0.514f, 0.455f, 1.0f},
    .function = {0.514f, 0.647f, 0.596f, 1.0f},
    .type = {0.980f, 0.741f, 0.184f, 1.0f},
    .number = {0.827f, 0.525f, 0.608f, 1.0f},
    .preproc = {0.847f, 0.600f, 0.129f, 1.0f},
};

typedef enum { MODE_NORMAL, MODE_INSERT, MODE_VISUAL, MODE_COMMAND } Mode;
typedef enum {
    HL_NORMAL,
    HL_KEYWORD,
    HL_STRING,
    HL_COMMENT,
    HL_FUNCTION,
    HL_TYPE,
    HL_NUMBER,
    HL_PREPROC
} HighlightKind;

typedef struct {
    char *data;
    size_t len, cap;
} Text;

typedef struct {
    size_t start;
    size_t end;
    HighlightKind kind;
} HighlightSpan;

typedef struct {
    HighlightSpan *spans;
    size_t len, cap;
} Highlights;

typedef struct {
    Text text;
    char path[512];
    bool dirty;
    size_t cursor;
    size_t visual_anchor;
    int desired_col;
    int top_line;
    int left_col;
    TSTree *tree;
    Highlights highlights;
} BufferSlot;

typedef struct {
    Text text;
    char path[512];
    bool dirty;
    size_t cursor;
    size_t visual_anchor;
    Mode mode;
    int desired_col;
    int top_line;
    int left_col;
    char pending;
    bool suppress_next_char;
    char command[256];
    size_t command_len;
    Text yank;
    bool has_yank;
    TSLanguage *c_lang;
    TSParser *parser;
    TSQuery *query;
    TSTree *tree;
    Highlights highlights;
    BufferSlot *buffers;
    size_t buffer_count;
    size_t current_buffer;
} Editor;

typedef struct {
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
    float advance, bearing_x, bearing_y;
} Glyph;

typedef struct {
    Glyph glyphs[128];
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkSampler sampler;
    float line_h;
    float cell_w;
    float ascent;
} FontAtlas;

typedef struct {
    float x, y;
    float u, v;
    float r, g, b, a;
    float use_tex;
} Vertex;

typedef struct {
    Vertex *vertices;
    uint32_t count;
    uint32_t cap;
} DrawList;

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
} GpuBuffer;

typedef struct {
    VkImage image;
    VkImageView view;
    VkFramebuffer framebuffer;
} SwapImage;

typedef struct {
    GLFWwindow *window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    uint32_t graphics_family;
    uint32_t present_family;
    VkSwapchainKHR swapchain;
    VkFormat swap_format;
    VkExtent2D extent;
    SwapImage *images;
    uint32_t image_count;
    VkRenderPass render_pass;
    VkDescriptorSetLayout desc_layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkDescriptorPool desc_pool;
    VkDescriptorSet desc_set;
    VkCommandPool cmd_pool;
    VkCommandBuffer *cmds;
    GpuBuffer vertex_buffers[MAX_FRAMES];
    VkSemaphore image_available[MAX_FRAMES];
    VkSemaphore render_finished[MAX_FRAMES];
    VkFence in_flight[MAX_FRAMES];
    uint32_t frame;
    bool framebuffer_resized;
    FontAtlas font;
} VkApp;

typedef struct {
    Editor editor;
    VkApp vk;
} App;

static void die(const char *msg) {
    fprintf(stderr, "med: %s\n", msg);
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) die("out of memory");
    return p;
}

static char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = xmalloc((size_t)n + 1);
    if (fread(data, 1, (size_t)n, f) != (size_t)n) die("failed to read file");
    fclose(f);
    data[n] = 0;
    *len_out = (size_t)n;
    return data;
}

static bool write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    return ok;
}

static void text_init(Text *t) {
    t->cap = 4096;
    t->len = 0;
    t->data = xmalloc(t->cap);
    t->data[0] = 0;
}

static void text_reserve(Text *t, size_t cap) {
    if (cap <= t->cap) return;
    while (t->cap < cap) t->cap *= 2;
    t->data = realloc(t->data, t->cap);
    if (!t->data) die("out of memory");
}

static void text_set(Text *t, const char *s, size_t n) {
    text_reserve(t, n + 1);
    memcpy(t->data, s, n);
    t->data[n] = 0;
    t->len = n;
}

static void text_insert(Text *t, size_t pos, const char *s, size_t n) {
    if (pos > t->len) pos = t->len;
    text_reserve(t, t->len + n + 1);
    memmove(t->data + pos + n, t->data + pos, t->len - pos + 1);
    memcpy(t->data + pos, s, n);
    t->len += n;
}

static void text_delete(Text *t, size_t pos, size_t n) {
    if (pos >= t->len) return;
    if (pos + n > t->len) n = t->len - pos;
    memmove(t->data + pos, t->data + pos + n, t->len - pos - n + 1);
    t->len -= n;
}

static size_t line_start_at(const Text *t, size_t pos) {
    if (pos > t->len) pos = t->len;
    while (pos > 0 && t->data[pos - 1] != '\n') pos--;
    return pos;
}

static size_t line_end_at(const Text *t, size_t pos) {
    if (pos > t->len) pos = t->len;
    while (pos < t->len && t->data[pos] != '\n') pos++;
    return pos;
}

static int byte_line(const Text *t, size_t pos) {
    int line = 0;
    if (pos > t->len) pos = t->len;
    for (size_t i = 0; i < pos; i++) {
        if (t->data[i] == '\n') line++;
    }
    return line;
}

static int byte_col(const Text *t, size_t pos) {
    return (int)(pos - line_start_at(t, pos));
}

static size_t line_start_by_number(const Text *t, int line) {
    if (line <= 0) return 0;
    int cur = 0;
    for (size_t i = 0; i < t->len; i++) {
        if (t->data[i] == '\n') {
            cur++;
            if (cur == line) return i + 1;
        }
    }
    return t->len;
}

static int line_count(const Text *t) {
    int lines = 1;
    for (size_t i = 0; i < t->len; i++) {
        if (t->data[i] == '\n') lines++;
    }
    return lines;
}

static size_t clamp_cursor_for_normal(const Text *t, size_t pos) {
    size_t start = line_start_at(t, pos);
    size_t end = line_end_at(t, pos);
    if (end > start && pos >= end) return end - 1;
    return pos;
}

static void editor_move_to_line_col(Editor *e, int line, int col) {
    if (line < 0) line = 0;
    int lc = line_count(&e->text);
    if (line >= lc) line = lc - 1;
    size_t start = line_start_by_number(&e->text, line);
    size_t end = line_end_at(&e->text, start);
    size_t pos = start + (size_t)col;
    if (pos > end) pos = end;
    e->cursor = pos;
    if (e->mode != MODE_INSERT) e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
}

static void editor_move_left(Editor *e) {
    if (e->cursor == 0) return;
    if (e->text.data[e->cursor - 1] == '\n') return;
    e->cursor--;
    e->desired_col = byte_col(&e->text, e->cursor);
}

static void editor_move_right(Editor *e) {
    if (e->cursor >= e->text.len) return;
    if (e->text.data[e->cursor] == '\n') return;
    e->cursor++;
    if (e->mode != MODE_INSERT) e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
    e->desired_col = byte_col(&e->text, e->cursor);
}

static void editor_move_vertical(Editor *e, int delta) {
    int line = byte_line(&e->text, e->cursor);
    int col = e->desired_col >= 0 ? e->desired_col : byte_col(&e->text, e->cursor);
    editor_move_to_line_col(e, line + delta, col);
}

static bool is_word_byte(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static void editor_move_line_start(Editor *e) {
    e->cursor = line_start_at(&e->text, e->cursor);
    e->desired_col = 0;
}

static void editor_move_line_end(Editor *e) {
    e->cursor = line_end_at(&e->text, e->cursor);
    if (e->mode != MODE_INSERT) e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
    e->desired_col = byte_col(&e->text, e->cursor);
}

static void editor_move_file_start(Editor *e) {
    e->cursor = 0;
    e->desired_col = 0;
}

static void editor_move_file_end(Editor *e) {
    e->cursor = clamp_cursor_for_normal(&e->text, e->text.len);
    e->desired_col = byte_col(&e->text, e->cursor);
}

static void editor_move_word_forward(Editor *e) {
    size_t p = e->cursor;
    if (p < e->text.len) p++;
    while (p < e->text.len && !is_word_byte(e->text.data[p])) p++;
    while (p < e->text.len && p > 0 && is_word_byte(e->text.data[p - 1]) && is_word_byte(e->text.data[p])) {
        if (!is_word_byte(e->text.data[p - 1])) break;
        break;
    }
    e->cursor = clamp_cursor_for_normal(&e->text, p);
    e->desired_col = byte_col(&e->text, e->cursor);
}

static void editor_move_word_back(Editor *e) {
    if (e->cursor == 0) return;
    size_t p = e->cursor - 1;
    while (p > 0 && !is_word_byte(e->text.data[p])) p--;
    while (p > 0 && is_word_byte(e->text.data[p - 1])) p--;
    e->cursor = p;
    e->desired_col = byte_col(&e->text, e->cursor);
}

static void editor_move_word_end(Editor *e) {
    size_t p = e->cursor;
    if (p < e->text.len) p++;
    while (p < e->text.len && !is_word_byte(e->text.data[p])) p++;
    while (p + 1 < e->text.len && is_word_byte(e->text.data[p + 1])) p++;
    e->cursor = clamp_cursor_for_normal(&e->text, p);
    e->desired_col = byte_col(&e->text, e->cursor);
}

static void highlights_clear(Highlights *h) {
    h->len = 0;
}

static void highlights_push(Highlights *h, size_t start, size_t end, HighlightKind kind) {
    if (start >= end) return;
    if (h->len == h->cap) {
        h->cap = h->cap ? h->cap * 2 : 256;
        h->spans = realloc(h->spans, h->cap * sizeof(*h->spans));
        if (!h->spans) die("out of memory");
    }
    h->spans[h->len++] = (HighlightSpan){start, end, kind};
}

static int span_cmp(const void *a, const void *b) {
    const HighlightSpan *sa = a;
    const HighlightSpan *sb = b;
    if (sa->start < sb->start) return -1;
    if (sa->start > sb->start) return 1;
    return (int)sa->kind - (int)sb->kind;
}

static HighlightKind highlight_at(const Editor *e, size_t byte) {
    for (size_t i = 0; i < e->highlights.len; i++) {
        HighlightSpan s = e->highlights.spans[i];
        if (byte >= s.start && byte < s.end) return s.kind;
        if (s.start > byte) break;
    }
    return HL_NORMAL;
}

static HighlightKind capture_kind(const char *name) {
    if (strstr(name, "keyword")) return HL_KEYWORD;
    if (strstr(name, "string")) return HL_STRING;
    if (strstr(name, "comment")) return HL_COMMENT;
    if (strstr(name, "function")) return HL_FUNCTION;
    if (strstr(name, "type")) return HL_TYPE;
    if (strstr(name, "number")) return HL_NUMBER;
    if (strstr(name, "preproc")) return HL_PREPROC;
    return HL_NORMAL;
}

static void editor_reparse(Editor *e) {
    highlights_clear(&e->highlights);
    if (!e->parser || !e->query) return;
    if (e->tree) ts_tree_delete(e->tree);
    e->tree = ts_parser_parse_string(e->parser, NULL, e->text.data, (uint32_t)e->text.len);
    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, e->query, ts_tree_root_node(e->tree));
    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        for (uint16_t i = 0; i < match.capture_count; i++) {
            TSQueryCapture cap = match.captures[i];
            uint32_t len = 0;
            const char *name = ts_query_capture_name_for_id(e->query, cap.index, &len);
            char buf[64];
            size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
            memcpy(buf, name, n);
            buf[n] = 0;
            HighlightKind kind = capture_kind(buf);
            if (kind != HL_NORMAL) {
                TSNode node = cap.node;
                highlights_push(&e->highlights, ts_node_start_byte(node), ts_node_end_byte(node), kind);
            }
        }
    }
    ts_query_cursor_delete(cursor);
    qsort(e->highlights.spans, e->highlights.len, sizeof(*e->highlights.spans), span_cmp);
}

static void editor_init_treesitter(Editor *e) {
    e->parser = ts_parser_new();
    e->c_lang = (TSLanguage *)tree_sitter_c();
    ts_parser_set_language(e->parser, e->c_lang);
    const char *query_src =
        "(comment) @comment\n"
        "(string_literal) @string\n"
        "(system_lib_string) @string\n"
        "(char_literal) @string\n"
        "(number_literal) @number\n"
        "(preproc_include) @preproc\n"
        "(preproc_def) @preproc\n"
        "(primitive_type) @type\n"
        "(type_identifier) @type\n"
        "(call_expression function: (identifier) @function)\n"
        "(function_declarator declarator: (identifier) @function)\n"
        "[\"if\" \"else\" \"for\" \"while\" \"do\" \"switch\" \"case\" \"default\" \"return\" \"break\" \"continue\" \"goto\" \"sizeof\" \"typedef\" \"struct\" \"enum\" \"union\" \"static\" \"extern\" \"const\" \"volatile\" \"inline\"] @keyword\n";
    uint32_t err_offset = 0;
    TSQueryError err_type = TSQueryErrorNone;
    e->query = ts_query_new(e->c_lang, query_src, (uint32_t)strlen(query_src), &err_offset, &err_type);
    if (!e->query) {
        fprintf(stderr, "tree-sitter query error at %u type %d\n", err_offset, err_type);
    }
}

static void editor_store_current_buffer(Editor *e) {
    if (!e->buffers || e->current_buffer >= e->buffer_count) return;
    BufferSlot *b = &e->buffers[e->current_buffer];
    b->text = e->text;
    memcpy(b->path, e->path, sizeof(b->path));
    b->dirty = e->dirty;
    b->cursor = e->cursor;
    b->visual_anchor = e->visual_anchor;
    b->desired_col = e->desired_col;
    b->top_line = e->top_line;
    b->left_col = e->left_col;
    b->tree = e->tree;
    b->highlights = e->highlights;
}

static void editor_load_buffer(Editor *e, size_t index) {
    if (!e->buffers || index >= e->buffer_count) return;
    editor_store_current_buffer(e);
    e->current_buffer = index;
    BufferSlot *b = &e->buffers[index];
    e->text = b->text;
    memcpy(e->path, b->path, sizeof(e->path));
    e->dirty = b->dirty;
    e->cursor = b->cursor;
    e->visual_anchor = b->visual_anchor;
    e->desired_col = b->desired_col;
    e->top_line = b->top_line;
    e->left_col = b->left_col;
    e->tree = b->tree;
    e->highlights = b->highlights;
    e->mode = MODE_NORMAL;
    e->pending = 0;
}

static void editor_add_buffer(Editor *e, const char *path, const char *content, size_t len) {
    e->buffers = realloc(e->buffers, sizeof(*e->buffers) * (e->buffer_count + 1));
    if (!e->buffers) die("out of memory");
    BufferSlot *b = &e->buffers[e->buffer_count];
    memset(b, 0, sizeof(*b));
    text_init(&b->text);
    text_set(&b->text, content, len);
    if (path) snprintf(b->path, sizeof(b->path), "%s", path);
    b->desired_col = -1;
    e->buffer_count++;
}

static void editor_switch_relative_buffer(Editor *e, int delta) {
    if (e->buffer_count == 0) return;
    size_t next = (e->current_buffer + e->buffer_count + (size_t)(delta > 0 ? 1 : e->buffer_count - 1)) % e->buffer_count;
    editor_load_buffer(e, next);
}

static bool editor_save_current(Editor *e, const char *path) {
    const char *target = path && path[0] ? path : e->path;
    if (!target[0]) return false;
    if (!write_file(target, e->text.data, e->text.len)) return false;
    snprintf(e->path, sizeof(e->path), "%s", target);
    e->dirty = false;
    editor_store_current_buffer(e);
    return true;
}

static void editor_init(Editor *e, const char *path) {
    memset(e, 0, sizeof(*e));
    text_init(&e->text);
    text_init(&e->yank);
    e->mode = MODE_NORMAL;
    e->desired_col = -1;
    const char *sample =
        "#include <stdio.h>\n\n"
        "int main(void) {\n"
        "    printf(\"med: Vulkan text editor\\n\");\n"
        "    return 0;\n"
        "}\n";
    if (path) {
        size_t n = 0;
        char *data = read_file(path, &n);
        if (data) {
            text_set(&e->text, data, n);
            snprintf(e->path, sizeof(e->path), "%s", path);
            free(data);
        } else {
            text_set(&e->text, sample, strlen(sample));
        }
    } else {
        text_set(&e->text, sample, strlen(sample));
    }
    editor_init_treesitter(e);
    editor_reparse(e);
    e->buffers = calloc(1, sizeof(*e->buffers));
    if (!e->buffers) die("out of memory");
    e->buffer_count = 1;
    e->current_buffer = 0;
    editor_store_current_buffer(e);
}

static void editor_ensure_visible(Editor *e, int rows, int cols) {
    int line = byte_line(&e->text, e->cursor);
    int col = byte_col(&e->text, e->cursor);
    if (line < e->top_line) e->top_line = line;
    if (line >= e->top_line + rows) e->top_line = line - rows + 1;
    if (e->top_line < 0) e->top_line = 0;
    if (col < e->left_col) e->left_col = col;
    if (col >= e->left_col + cols) e->left_col = col - cols + 1;
    if (e->left_col < 0) e->left_col = 0;
}

static void editor_insert_char(Editor *e, unsigned int cp) {
    if (cp < 32 || cp > 126) return;
    char c = (char)cp;
    text_insert(&e->text, e->cursor, &c, 1);
    e->cursor++;
    e->desired_col = byte_col(&e->text, e->cursor);
    e->dirty = true;
    editor_reparse(e);
}

static void editor_backspace(Editor *e) {
    if (e->cursor == 0) return;
    text_delete(&e->text, e->cursor - 1, 1);
    e->cursor--;
    e->desired_col = byte_col(&e->text, e->cursor);
    e->dirty = true;
    editor_reparse(e);
}

static void editor_delete_char(Editor *e) {
    if (e->cursor >= e->text.len) return;
    text_delete(&e->text, e->cursor, 1);
    e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
    e->dirty = true;
    editor_reparse(e);
}

static void editor_newline(Editor *e) {
    text_insert(&e->text, e->cursor, "\n", 1);
    e->cursor++;
    e->desired_col = 0;
    e->dirty = true;
    editor_reparse(e);
}

static void editor_delete_line(Editor *e) {
    size_t start = line_start_at(&e->text, e->cursor);
    size_t end = line_end_at(&e->text, e->cursor);
    if (end < e->text.len) end++;
    if (start == end && e->text.len) end = e->text.len;
    text_delete(&e->text, start, end - start);
    if (start > e->text.len) start = e->text.len;
    e->cursor = clamp_cursor_for_normal(&e->text, start);
    e->desired_col = 0;
    e->dirty = true;
    editor_reparse(e);
}

static void editor_yank_range(Editor *e, size_t start, size_t end) {
    if (end < start) return;
    text_set(&e->yank, e->text.data + start, end - start);
    e->has_yank = true;
}

static void editor_yank_line(Editor *e) {
    size_t start = line_start_at(&e->text, e->cursor);
    size_t end = line_end_at(&e->text, e->cursor);
    if (end < e->text.len) end++;
    editor_yank_range(e, start, end);
}

static void editor_paste_after(Editor *e) {
    if (!e->has_yank || e->yank.len == 0) return;
    size_t pos = e->cursor;
    if (memchr(e->yank.data, '\n', e->yank.len)) {
        pos = line_end_at(&e->text, e->cursor);
        if (pos < e->text.len) pos++;
    } else if (pos < e->text.len) {
        pos++;
    }
    text_insert(&e->text, pos, e->yank.data, e->yank.len);
    e->cursor = clamp_cursor_for_normal(&e->text, pos);
    e->dirty = true;
    editor_reparse(e);
}

static void editor_delete_selection(Editor *e) {
    size_t a = e->visual_anchor;
    size_t b = e->cursor;
    if (a > b) {
        size_t tmp = a;
        a = b;
        b = tmp;
    }
    if (b < e->text.len) b++;
    text_delete(&e->text, a, b - a);
    e->cursor = clamp_cursor_for_normal(&e->text, a);
    e->mode = MODE_NORMAL;
    e->dirty = true;
    editor_reparse(e);
}

static void editor_key(Editor *e, int key, int mods) {
    bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    if (key == GLFW_KEY_ESCAPE) {
        if (e->mode == MODE_INSERT && e->cursor > 0) e->cursor--;
        e->mode = MODE_NORMAL;
        e->pending = 0;
        e->command_len = 0;
        e->command[0] = 0;
        e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
        e->desired_col = byte_col(&e->text, e->cursor);
        return;
    }
    if (e->mode == MODE_INSERT) {
        if (key == GLFW_KEY_BACKSPACE) editor_backspace(e);
        else if (key == GLFW_KEY_ENTER) editor_newline(e);
        else if (key == GLFW_KEY_LEFT) editor_move_left(e);
        else if (key == GLFW_KEY_RIGHT) editor_move_right(e);
        else if (key == GLFW_KEY_UP) editor_move_vertical(e, -1);
        else if (key == GLFW_KEY_DOWN) editor_move_vertical(e, 1);
        return;
    }
    if (key == GLFW_KEY_H || key == GLFW_KEY_LEFT) editor_move_left(e);
    else if (key == GLFW_KEY_L || key == GLFW_KEY_RIGHT) editor_move_right(e);
    else if (key == GLFW_KEY_K || key == GLFW_KEY_UP) editor_move_vertical(e, -1);
    else if (key == GLFW_KEY_J || key == GLFW_KEY_DOWN) editor_move_vertical(e, 1);
    else if (key == GLFW_KEY_0) editor_move_line_start(e);
    else if (key == GLFW_KEY_4 && shift) editor_move_line_end(e);
    else if (key == GLFW_KEY_W) editor_move_word_forward(e);
    else if (key == GLFW_KEY_B) editor_move_word_back(e);
    else if (key == GLFW_KEY_E) editor_move_word_end(e);
    else if (key == GLFW_KEY_G) {
        if (shift) {
            editor_move_file_end(e);
            e->pending = 0;
        } else if (e->pending == 'g') {
            editor_move_file_start(e);
            e->pending = 0;
        } else {
            e->pending = 'g';
        }
    }
    else if (key == GLFW_KEY_I) {
        if (shift) editor_move_line_start(e);
        e->mode = MODE_INSERT;
        e->pending = 0;
        e->suppress_next_char = true;
    } else if (key == GLFW_KEY_A) {
        if (shift) editor_move_line_end(e);
        if (e->cursor < e->text.len && e->text.data[e->cursor] != '\n') e->cursor++;
        e->mode = MODE_INSERT;
        e->pending = 0;
        e->suppress_next_char = true;
    } else if (key == GLFW_KEY_V) {
        e->mode = MODE_VISUAL;
        e->visual_anchor = e->cursor;
        e->pending = 0;
    } else if (key == GLFW_KEY_X) {
        if (e->mode == MODE_VISUAL) editor_delete_selection(e);
        else editor_delete_char(e);
        e->pending = 0;
    } else if (key == GLFW_KEY_D) {
        if (e->mode == MODE_VISUAL) {
            editor_delete_selection(e);
        } else if (shift) {
            size_t end = line_end_at(&e->text, e->cursor);
            editor_yank_range(e, e->cursor, end);
            text_delete(&e->text, e->cursor, end - e->cursor);
            e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
            e->dirty = true;
            editor_reparse(e);
        } else if (e->pending == 'd') {
            editor_yank_line(e);
            editor_delete_line(e);
            e->pending = 0;
        } else {
            e->pending = 'd';
        }
    } else if (key == GLFW_KEY_Y) {
        if (e->mode == MODE_VISUAL) {
            size_t a = e->visual_anchor, b = e->cursor;
            if (a > b) {
                size_t t = a;
                a = b;
                b = t;
            }
            if (b < e->text.len) b++;
            editor_yank_range(e, a, b);
            e->mode = MODE_NORMAL;
            e->pending = 0;
        } else if (e->pending == 'y') {
            editor_yank_line(e);
            e->pending = 0;
        } else {
            e->pending = 'y';
        }
    } else if (key == GLFW_KEY_P) {
        editor_paste_after(e);
        e->pending = 0;
    } else if (key == GLFW_KEY_O && e->mode == MODE_NORMAL) {
        if (shift) {
            size_t start = line_start_at(&e->text, e->cursor);
            text_insert(&e->text, start, "\n", 1);
            e->cursor = start;
            e->desired_col = 0;
            e->dirty = true;
            editor_reparse(e);
        } else {
            size_t end = line_end_at(&e->text, e->cursor);
            e->cursor = end;
            editor_newline(e);
        }
        e->mode = MODE_INSERT;
        e->pending = 0;
        e->suppress_next_char = true;
    } else if (key == GLFW_KEY_SEMICOLON && shift) {
        e->mode = MODE_COMMAND;
        e->command_len = 0;
        e->command[0] = 0;
        e->pending = 0;
        e->suppress_next_char = true;
    } else if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_D) {
        e->top_line += 10;
    } else if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_U) {
        e->top_line -= 10;
        if (e->top_line < 0) e->top_line = 0;
    } else {
        e->pending = 0;
    }
}

static Color color_for_highlight(HighlightKind kind) {
    switch (kind) {
    case HL_KEYWORD: return gruvbox.keyword;
    case HL_STRING: return gruvbox.string;
    case HL_COMMENT: return gruvbox.comment;
    case HL_FUNCTION: return gruvbox.function;
    case HL_TYPE: return gruvbox.type;
    case HL_NUMBER: return gruvbox.number;
    case HL_PREPROC: return gruvbox.preproc;
    default: return gruvbox.fg;
    }
}

static uint32_t find_memory(VkPhysicalDevice physical, uint32_t type_bits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(physical, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    die("no suitable memory type");
    return 0;
}

static void create_buffer(VkApp *app, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, GpuBuffer *out) {
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (vkCreateBuffer(app->device, &bi, NULL, &out->buffer) != VK_SUCCESS) die("vkCreateBuffer failed");
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(app->device, out->buffer, &req);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = find_memory(app->physical, req.memoryTypeBits, props)};
    if (vkAllocateMemory(app->device, &ai, NULL, &out->memory) != VK_SUCCESS) die("vkAllocateMemory buffer failed");
    vkBindBufferMemory(app->device, out->buffer, out->memory, 0);
}

static VkCommandBuffer begin_one_time(VkApp *app) {
    VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = app->cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(app->device, &ai, &cmd);
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

static void end_one_time(VkApp *app, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    vkQueueSubmit(app->graphics_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(app->graphics_queue);
    vkFreeCommandBuffers(app->device, app->cmd_pool, 1, &cmd);
}

static void transition_image(VkApp *app, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout) {
    VkCommandBuffer cmd = begin_one_time(app);
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
    };
    VkPipelineStageFlags src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, NULL, 0, NULL, 1, &barrier);
    end_one_time(app, cmd);
}

static void copy_buffer_to_image(VkApp *app, VkBuffer buffer, VkImage image, uint32_t w, uint32_t h) {
    VkCommandBuffer cmd = begin_one_time(app);
    VkBufferImageCopy region = {
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .imageExtent = {w, h, 1},
    };
    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    end_one_time(app, cmd);
}

static VkShaderModule load_shader(VkApp *app, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", MED_SHADER_DIR, name);
    size_t len = 0;
    char *bytes = read_file(path, &len);
    if (!bytes) die("failed to read shader");
    VkShaderModuleCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = len, .pCode = (const uint32_t *)bytes};
    VkShaderModule mod;
    if (vkCreateShaderModule(app->device, &ci, NULL, &mod) != VK_SUCCESS) die("vkCreateShaderModule failed");
    free(bytes);
    return mod;
}

static void font_create(VkApp *app) {
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) die("FreeType init failed");
    FT_Face face;
    if (FT_New_Face(ft, MED_DEFAULT_FONT, 0, &face)) die("failed to load CaskaydiaCove font");
    FT_Set_Pixel_Sizes(face, 0, MED_FONT_SIZE);
    unsigned char *pixels = calloc(ATLAS_W * ATLAS_H, 1);
    if (!pixels) die("out of memory");
    int pen_x = 1, pen_y = 1, row_h = 0;
    app->font.ascent = (float)(face->size->metrics.ascender >> 6);
    app->font.line_h = (float)((face->size->metrics.height >> 6) + 4);
    app->font.cell_w = 8.0f;
    for (int c = 32; c < 127; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) continue;
        FT_GlyphSlot g = face->glyph;
        if (pen_x + (int)g->bitmap.width + 1 >= ATLAS_W) {
            pen_x = 1;
            pen_y += row_h + 1;
            row_h = 0;
        }
        for (int y = 0; y < (int)g->bitmap.rows; y++) {
            memcpy(pixels + (pen_y + y) * ATLAS_W + pen_x, g->bitmap.buffer + y * g->bitmap.pitch, g->bitmap.width);
        }
        Glyph *gl = &app->font.glyphs[c];
        gl->u0 = (float)pen_x / ATLAS_W;
        gl->v0 = (float)pen_y / ATLAS_H;
        gl->u1 = (float)(pen_x + g->bitmap.width) / ATLAS_W;
        gl->v1 = (float)(pen_y + g->bitmap.rows) / ATLAS_H;
        gl->x0 = 0;
        gl->y0 = 0;
        gl->x1 = (float)g->bitmap.width;
        gl->y1 = (float)g->bitmap.rows;
        gl->advance = (float)(g->advance.x >> 6);
        gl->bearing_x = (float)g->bitmap_left;
        gl->bearing_y = (float)g->bitmap_top;
        if (c == 'M') app->font.cell_w = gl->advance;
        pen_x += (int)g->bitmap.width + 1;
        if ((int)g->bitmap.rows > row_h) row_h = (int)g->bitmap.rows;
    }
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    VkImageCreateInfo ii = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent = {ATLAS_W, ATLAS_H, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(app->device, &ii, NULL, &app->font.image) != VK_SUCCESS) die("vkCreateImage font failed");
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(app->device, app->font.image, &req);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = find_memory(app->physical, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    if (vkAllocateMemory(app->device, &ai, NULL, &app->font.memory) != VK_SUCCESS) die("font memory failed");
    vkBindImageMemory(app->device, app->font.image, app->font.memory, 0);

    GpuBuffer staging;
    create_buffer(app, ATLAS_W * ATLAS_H, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging);
    void *mapped;
    vkMapMemory(app->device, staging.memory, 0, ATLAS_W * ATLAS_H, 0, &mapped);
    memcpy(mapped, pixels, ATLAS_W * ATLAS_H);
    vkUnmapMemory(app->device, staging.memory);
    transition_image(app, app->font.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(app, staging.buffer, app->font.image, ATLAS_W, ATLAS_H);
    transition_image(app, app->font.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkDestroyBuffer(app->device, staging.buffer, NULL);
    vkFreeMemory(app->device, staging.memory, NULL);
    free(pixels);

    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = app->font.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
    };
    vkCreateImageView(app->device, &vi, NULL, &app->font.view);
    VkSamplerCreateInfo si = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    };
    vkCreateSampler(app->device, &si, NULL, &app->font.sampler);
}

static bool queue_supports_present(VkApp *app, uint32_t family) {
    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(app->physical, family, app->surface, &present);
    return present == VK_TRUE;
}

static void pick_physical(VkApp *app) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(app->instance, &count, NULL);
    if (!count) die("no Vulkan physical device");
    VkPhysicalDevice *devices = xmalloc(sizeof(*devices) * count);
    vkEnumeratePhysicalDevices(app->instance, &count, devices);
    for (uint32_t d = 0; d < count; d++) {
        app->physical = devices[d];
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(app->physical, &qcount, NULL);
        VkQueueFamilyProperties *qs = xmalloc(sizeof(*qs) * qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(app->physical, &qcount, qs);
        bool got_g = false, got_p = false;
        for (uint32_t i = 0; i < qcount; i++) {
            if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && !got_g) {
                app->graphics_family = i;
                got_g = true;
            }
            if (queue_supports_present(app, i) && !got_p) {
                app->present_family = i;
                got_p = true;
            }
        }
        free(qs);
        if (got_g && got_p) {
            free(devices);
            return;
        }
    }
    free(devices);
    die("no suitable Vulkan device");
}

static void create_device(VkApp *app) {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci[2] = {
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = app->graphics_family, .queueCount = 1, .pQueuePriorities = &priority},
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = app->present_family, .queueCount = 1, .pQueuePriorities = &priority},
    };
    uint32_t qn = app->graphics_family == app->present_family ? 1 : 2;
    const char *exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = qn, .pQueueCreateInfos = qci, .enabledExtensionCount = 1, .ppEnabledExtensionNames = exts};
    if (vkCreateDevice(app->physical, &ci, NULL, &app->device) != VK_SUCCESS) die("vkCreateDevice failed");
    vkGetDeviceQueue(app->device, app->graphics_family, 0, &app->graphics_queue);
    vkGetDeviceQueue(app->device, app->present_family, 0, &app->present_queue);
}

static VkSurfaceFormatKHR choose_format(VkSurfaceFormatKHR *formats, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return formats[i];
    }
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return formats[i];
    }
    return formats[0];
}

static void create_swapchain(VkApp *app) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physical, app->surface, &caps);
    uint32_t fcount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physical, app->surface, &fcount, NULL);
    VkSurfaceFormatKHR *formats = xmalloc(sizeof(*formats) * fcount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physical, app->surface, &fcount, formats);
    VkSurfaceFormatKHR fmt = choose_format(formats, fcount);
    free(formats);
    app->swap_format = fmt.format;
    if (caps.currentExtent.width != UINT32_MAX) {
        app->extent = caps.currentExtent;
    } else {
        int w, h;
        glfwGetFramebufferSize(app->window, &w, &h);
        app->extent.width = (uint32_t)w;
        app->extent.height = (uint32_t)h;
    }
    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
    uint32_t families[] = {app->graphics_family, app->present_family};
    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = app->surface,
        .minImageCount = image_count,
        .imageFormat = app->swap_format,
        .imageColorSpace = fmt.colorSpace,
        .imageExtent = app->extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    if (app->graphics_family != app->present_family) {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = families;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    if (vkCreateSwapchainKHR(app->device, &ci, NULL, &app->swapchain) != VK_SUCCESS) die("vkCreateSwapchainKHR failed");
    vkGetSwapchainImagesKHR(app->device, app->swapchain, &app->image_count, NULL);
    VkImage *imgs = xmalloc(sizeof(*imgs) * app->image_count);
    vkGetSwapchainImagesKHR(app->device, app->swapchain, &app->image_count, imgs);
    app->images = calloc(app->image_count, sizeof(*app->images));
    for (uint32_t i = 0; i < app->image_count; i++) {
        app->images[i].image = imgs[i];
        VkImageViewCreateInfo vi = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = imgs[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = app->swap_format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
        };
        vkCreateImageView(app->device, &vi, NULL, &app->images[i].view);
    }
    free(imgs);
}

static void create_render_pass(VkApp *app) {
    VkAttachmentDescription color = {
        .format = app->swap_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &ref};
    VkSubpassDependency dep = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &color, .subpassCount = 1, .pSubpasses = &sub, .dependencyCount = 1, .pDependencies = &dep};
    if (vkCreateRenderPass(app->device, &ci, NULL, &app->render_pass) != VK_SUCCESS) die("vkCreateRenderPass failed");
}

static void create_pipeline(VkApp *app) {
    VkDescriptorSetLayoutBinding b = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo dl = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &b};
    vkCreateDescriptorSetLayout(app->device, &dl, NULL, &app->desc_layout);
    VkPushConstantRange pc = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(float) * 2};
    VkPipelineLayoutCreateInfo pl = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &app->desc_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pc};
    vkCreatePipelineLayout(app->device, &pl, NULL, &app->pipeline_layout);

    VkShaderModule vs = load_shader(app, "text.vert.spv");
    VkShaderModule fs = load_shader(app, "text.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main"},
    };
    VkVertexInputBindingDescription binding = {.binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[4] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, x)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, u)},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex, r)},
        {.location = 3, .binding = 0, .format = VK_FORMAT_R32_SFLOAT, .offset = offsetof(Vertex, use_tex)},
    };
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding, .vertexAttributeDescriptionCount = 4, .pVertexAttributeDescriptions = attrs};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport = {.x = 0, .y = 0, .width = (float)app->extent.width, .height = (float)app->extent.height, .minDepth = 0, .maxDepth = 1};
    VkRect2D scissor = {.extent = app->extent};
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend = {.blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend};
    VkGraphicsPipelineCreateInfo gp = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = stages, .pVertexInputState = &vi, .pInputAssemblyState = &ia, .pViewportState = &vp, .pRasterizationState = &rs, .pMultisampleState = &ms, .pColorBlendState = &cb, .layout = app->pipeline_layout, .renderPass = app->render_pass};
    if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &gp, NULL, &app->pipeline) != VK_SUCCESS) die("vkCreateGraphicsPipelines failed");
    vkDestroyShaderModule(app->device, vs, NULL);
    vkDestroyShaderModule(app->device, fs, NULL);
}

static void create_framebuffers(VkApp *app) {
    for (uint32_t i = 0; i < app->image_count; i++) {
        VkImageView attachments[] = {app->images[i].view};
        VkFramebufferCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = app->render_pass, .attachmentCount = 1, .pAttachments = attachments, .width = app->extent.width, .height = app->extent.height, .layers = 1};
        vkCreateFramebuffer(app->device, &fi, NULL, &app->images[i].framebuffer);
    }
}

static void create_descriptors(VkApp *app) {
    VkDescriptorPoolSize size = {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1};
    VkDescriptorPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &size};
    vkCreateDescriptorPool(app->device, &pi, NULL, &app->desc_pool);
    VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = app->desc_pool, .descriptorSetCount = 1, .pSetLayouts = &app->desc_layout};
    vkAllocateDescriptorSets(app->device, &ai, &app->desc_set);
    VkDescriptorImageInfo img = {.sampler = app->font.sampler, .imageView = app->font.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = app->desc_set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &img};
    vkUpdateDescriptorSets(app->device, 1, &wr, 0, NULL);
}

static void create_commands_sync(VkApp *app) {
    VkCommandPoolCreateInfo cp = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = app->graphics_family, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    vkCreateCommandPool(app->device, &cp, NULL, &app->cmd_pool);
    app->cmds = xmalloc(sizeof(*app->cmds) * app->image_count);
    VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = app->cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = app->image_count};
    vkAllocateCommandBuffers(app->device, &ai, app->cmds);
    for (int i = 0; i < MAX_FRAMES; i++) {
        create_buffer(app, sizeof(Vertex) * MAX_VERTICES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->vertex_buffers[i]);
        VkSemaphoreCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
        vkCreateSemaphore(app->device, &si, NULL, &app->image_available[i]);
        vkCreateSemaphore(app->device, &si, NULL, &app->render_finished[i]);
        vkCreateFence(app->device, &fi, NULL, &app->in_flight[i]);
    }
}

static void framebuffer_cb(GLFWwindow *window, int w, int h) {
    (void)w;
    (void)h;
    App *app = glfwGetWindowUserPointer(window);
    app->vk.framebuffer_resized = true;
}

static char *trim_command(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

static void app_execute_command(App *app) {
    Editor *e = &app->editor;
    char tmp[sizeof(e->command)];
    snprintf(tmp, sizeof(tmp), "%s", e->command);
    char *cmd = trim_command(tmp);
    if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
        glfwSetWindowShouldClose(app->vk.window, GLFW_TRUE);
    } else if (strcmp(cmd, "w") == 0 || strncmp(cmd, "w ", 2) == 0 || strncmp(cmd, "write ", 6) == 0) {
        char *path = NULL;
        if (strncmp(cmd, "w ", 2) == 0) path = trim_command(cmd + 2);
        if (strncmp(cmd, "write ", 6) == 0) path = trim_command(cmd + 6);
        editor_save_current(e, path);
    } else if (strcmp(cmd, "wq") == 0) {
        if (editor_save_current(e, NULL)) glfwSetWindowShouldClose(app->vk.window, GLFW_TRUE);
    } else if (strncmp(cmd, "e ", 2) == 0 || strncmp(cmd, "edit ", 5) == 0) {
        char *path = trim_command(cmd + (cmd[0] == 'e' ? 2 : 5));
        size_t n = 0;
        char *data = read_file(path, &n);
        if (data) {
            editor_store_current_buffer(e);
            editor_add_buffer(e, path, data, n);
            free(data);
            editor_load_buffer(e, e->buffer_count - 1);
            editor_reparse(e);
            editor_store_current_buffer(e);
        }
    } else if (strcmp(cmd, "bnew") == 0 || strcmp(cmd, "new") == 0) {
        editor_store_current_buffer(e);
        editor_add_buffer(e, NULL, "", 0);
        editor_load_buffer(e, e->buffer_count - 1);
        editor_reparse(e);
        editor_store_current_buffer(e);
    } else if (strcmp(cmd, "bn") == 0 || strcmp(cmd, "bnext") == 0) {
        editor_switch_relative_buffer(e, 1);
    } else if (strcmp(cmd, "bp") == 0 || strcmp(cmd, "bprev") == 0) {
        editor_switch_relative_buffer(e, -1);
    }
    e->mode = MODE_NORMAL;
    e->command_len = 0;
    e->command[0] = 0;
    e->pending = 0;
}

static void char_cb(GLFWwindow *window, unsigned int cp) {
    App *app = glfwGetWindowUserPointer(window);
    Editor *e = &app->editor;
    if (e->suppress_next_char) {
        e->suppress_next_char = false;
        return;
    }
    if (e->mode == MODE_COMMAND) {
        if (cp >= 32 && cp <= 126 && e->command_len + 1 < sizeof(e->command)) {
            e->command[e->command_len++] = (char)cp;
            e->command[e->command_len] = 0;
        }
    } else if (e->mode == MODE_INSERT) {
        editor_insert_char(e, cp);
    } else if (e->mode == MODE_NORMAL && cp == ':') {
        e->mode = MODE_COMMAND;
        e->command_len = 0;
        e->command[0] = 0;
    }
}

static void key_cb(GLFWwindow *window, int key, int scancode, int action, int mods) {
    (void)scancode;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    App *app = glfwGetWindowUserPointer(window);
    if (app->editor.mode == MODE_COMMAND) {
        if (key == GLFW_KEY_ENTER) {
            app_execute_command(app);
            return;
        }
        if (key == GLFW_KEY_BACKSPACE && app->editor.command_len > 0) {
            app->editor.command[--app->editor.command_len] = 0;
            return;
        }
        return;
    }
    editor_key(&app->editor, key, mods);
}

static void vk_init(App *owner) {
    VkApp *app = &owner->vk;
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    app->window = glfwCreateWindow(1100, 760, "med", NULL, NULL);
    if (!app->window) die("failed to create window");
    glfwSetWindowUserPointer(app->window, owner);
    glfwSetFramebufferSizeCallback(app->window, framebuffer_cb);
    glfwSetCharCallback(app->window, char_cb);
    glfwSetKeyCallback(app->window, key_cb);
    uint32_t ext_count = 0;
    const char **exts = glfwGetRequiredInstanceExtensions(&ext_count);
    VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "med", .apiVersion = VK_API_VERSION_1_0};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &ai, .enabledExtensionCount = ext_count, .ppEnabledExtensionNames = exts};
    if (vkCreateInstance(&ici, NULL, &app->instance) != VK_SUCCESS) die("vkCreateInstance failed");
    if (glfwCreateWindowSurface(app->instance, app->window, NULL, &app->surface) != VK_SUCCESS) die("surface failed");
    pick_physical(app);
    create_device(app);
    create_swapchain(app);
    create_render_pass(app);
    create_commands_sync(app);
    font_create(app);
    create_pipeline(app);
    create_framebuffers(app);
    create_descriptors(app);
}

static void dl_push(DrawList *dl, Vertex v) {
    if (dl->count < dl->cap) dl->vertices[dl->count++] = v;
}

static void draw_rect(DrawList *dl, float x, float y, float w, float h, Color c) {
    Vertex v[6] = {
        {x, y, 0, 0, c.r, c.g, c.b, c.a, 0}, {x + w, y, 0, 0, c.r, c.g, c.b, c.a, 0}, {x + w, y + h, 0, 0, c.r, c.g, c.b, c.a, 0},
        {x, y, 0, 0, c.r, c.g, c.b, c.a, 0}, {x + w, y + h, 0, 0, c.r, c.g, c.b, c.a, 0}, {x, y + h, 0, 0, c.r, c.g, c.b, c.a, 0},
    };
    for (int i = 0; i < 6; i++) dl_push(dl, v[i]);
}

static void draw_glyph(DrawList *dl, FontAtlas *font, char ch, float x, float y, Color c) {
    if ((unsigned char)ch >= 127 || ch < 32) return;
    Glyph *g = &font->glyphs[(int)ch];
    float x0 = x + g->bearing_x;
    float y0 = y + font->ascent - g->bearing_y;
    float x1 = x0 + g->x1;
    float y1 = y0 + g->y1;
    Vertex v[6] = {
        {x0, y0, g->u0, g->v0, c.r, c.g, c.b, c.a, 1}, {x1, y0, g->u1, g->v0, c.r, c.g, c.b, c.a, 1}, {x1, y1, g->u1, g->v1, c.r, c.g, c.b, c.a, 1},
        {x0, y0, g->u0, g->v0, c.r, c.g, c.b, c.a, 1}, {x1, y1, g->u1, g->v1, c.r, c.g, c.b, c.a, 1}, {x0, y1, g->u0, g->v1, c.r, c.g, c.b, c.a, 1},
    };
    for (int i = 0; i < 6; i++) dl_push(dl, v[i]);
}

static void draw_text(DrawList *dl, FontAtlas *font, const char *s, float x, float y, Color c) {
    for (size_t i = 0; s[i]; i++) {
        draw_glyph(dl, font, s[i], x, y, c);
        x += font->cell_w;
    }
}

static bool in_selection(const Editor *e, size_t pos) {
    if (e->mode != MODE_VISUAL) return false;
    size_t a = e->visual_anchor, b = e->cursor;
    if (a > b) {
        size_t t = a;
        a = b;
        b = t;
    }
    return pos >= a && pos <= b;
}

static void build_draw_list(App *owner, DrawList *dl) {
    Editor *e = &owner->editor;
    VkApp *vk = &owner->vk;
    dl->count = 0;
    float w = (float)vk->extent.width;
    float h = (float)vk->extent.height;
    float line_h = vk->font.line_h;
    float cell = vk->font.cell_w;
    int rows = (int)((h - line_h) / line_h);
    if (rows < 1) rows = 1;
    int gutter_digits = (int)log10((double)(line_count(&e->text) + 1)) + 2;
    float gutter_w = (float)gutter_digits * cell + 18.0f;
    int cols = (int)((w - gutter_w - 12.0f) / cell);
    editor_ensure_visible(e, rows, cols);
    draw_rect(dl, 0, 0, w, h, gruvbox.bg);
    draw_rect(dl, 0, 0, gutter_w, h, gruvbox.gutter_bg);
    int cursor_line = byte_line(&e->text, e->cursor);
    for (int row = 0; row < rows; row++) {
        int line_no = e->top_line + row;
        if (line_no >= line_count(&e->text)) break;
        float y = row * line_h + 2.0f;
        char num[32];
        int rel = abs(line_no - cursor_line);
        int width = gutter_digits - 1;
        if (width > 16) width = 16;
        char raw[24];
        snprintf(raw, sizeof(raw), "%d", rel == 0 ? line_no + 1 : rel);
        int raw_len = (int)strlen(raw);
        int pad = width > raw_len ? width - raw_len : 0;
        memset(num, ' ', (size_t)pad);
        snprintf(num + pad, sizeof(num) - (size_t)pad, "%s", raw);
        draw_text(dl, &vk->font, num, 8.0f, y, rel == 0 ? gruvbox.line_no_current : gruvbox.gutter_fg);
        size_t start = line_start_by_number(&e->text, line_no);
        size_t end = line_end_at(&e->text, start);
        float x = gutter_w + 8.0f;
        for (size_t p = start + (size_t)e->left_col; p < end; p++) {
            if (in_selection(e, p)) draw_rect(dl, x, y, cell, line_h, gruvbox.selection);
            Color c = color_for_highlight(highlight_at(e, p));
            char ch = e->text.data[p];
            if (ch == '\t') {
                x += cell * 4.0f;
            } else {
                draw_glyph(dl, &vk->font, ch, x, y, c);
                x += cell;
            }
            if (x > w) break;
        }
    }
    int c_line = cursor_line - e->top_line;
    int c_col = byte_col(&e->text, e->cursor) - e->left_col;
    if (c_line >= 0 && c_line < rows && c_col >= 0) {
        float x = gutter_w + 8.0f + c_col * cell;
        float y = c_line * line_h + 2.0f;
        if (e->mode == MODE_INSERT) draw_rect(dl, x, y + 2.0f, 2.0f, line_h - 4.0f, gruvbox.cursor_insert);
        else draw_rect(dl, x, y, cell, line_h, gruvbox.cursor);
        if (e->mode != MODE_INSERT && e->cursor < e->text.len && e->text.data[e->cursor] != '\n') {
            Color c = color_for_highlight(highlight_at(e, e->cursor));
            draw_glyph(dl, &vk->font, e->text.data[e->cursor], x, y, c);
        }
    }
    const char *mode = e->mode == MODE_INSERT ? "-- INSERT --" : e->mode == MODE_VISUAL ? "-- VISUAL --" : e->mode == MODE_COMMAND ? ":" : "NORMAL";
    draw_rect(dl, 0, h - line_h, w, line_h, gruvbox.gutter_bg);
    if (e->mode == MODE_COMMAND) {
        char status[320];
        snprintf(status, sizeof(status), ":%s", e->command);
        draw_text(dl, &vk->font, status, 8.0f, h - line_h + 1.0f, gruvbox.line_no_current);
    } else {
        char status[768];
        const char *path = e->path[0] ? e->path : "[No Name]";
        snprintf(status, sizeof(status), "%s  [%zu/%zu]%s  %s", mode, e->current_buffer + 1, e->buffer_count, e->dirty ? " +" : "", path);
        draw_text(dl, &vk->font, status, 8.0f, h - line_h + 1.0f, gruvbox.line_no_current);
    }
}

static void record_cmd(VkApp *app, uint32_t image, uint32_t vcount) {
    VkCommandBuffer cmd = app->cmds[image];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &bi);
    VkClearValue clear = {.color = {{gruvbox.bg.r, gruvbox.bg.g, gruvbox.bg.b, 1.0f}}};
    VkRenderPassBeginInfo rp = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = app->render_pass, .framebuffer = app->images[image].framebuffer, .renderArea = {.extent = app->extent}, .clearValueCount = 1, .pClearValues = &clear};
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->pipeline_layout, 0, 1, &app->desc_set, 0, NULL);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &app->vertex_buffers[app->frame].buffer, &off);
    float screen[2] = {(float)app->extent.width, (float)app->extent.height};
    vkCmdPushConstants(cmd, app->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(screen), screen);
    vkCmdDraw(cmd, vcount, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

static void draw_frame(App *owner) {
    VkApp *app = &owner->vk;
    vkWaitForFences(app->device, 1, &app->in_flight[app->frame], VK_TRUE, UINT64_MAX);
    uint32_t image = 0;
    VkResult res = vkAcquireNextImageKHR(app->device, app->swapchain, UINT64_MAX, app->image_available[app->frame], VK_NULL_HANDLE, &image);
    if (res != VK_SUCCESS) return;
    vkResetFences(app->device, 1, &app->in_flight[app->frame]);
    DrawList dl = {.vertices = xmalloc(sizeof(Vertex) * MAX_VERTICES), .cap = MAX_VERTICES};
    build_draw_list(owner, &dl);
    void *mapped;
    vkMapMemory(app->device, app->vertex_buffers[app->frame].memory, 0, sizeof(Vertex) * dl.count, 0, &mapped);
    memcpy(mapped, dl.vertices, sizeof(Vertex) * dl.count);
    vkUnmapMemory(app->device, app->vertex_buffers[app->frame].memory);
    record_cmd(app, image, dl.count);
    free(dl.vertices);
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &app->image_available[app->frame],
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &app->cmds[image],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &app->render_finished[app->frame],
    };
    if (vkQueueSubmit(app->graphics_queue, 1, &submit, app->in_flight[app->frame]) != VK_SUCCESS) die("vkQueueSubmit failed");
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &app->render_finished[app->frame], .swapchainCount = 1, .pSwapchains = &app->swapchain, .pImageIndices = &image};
    vkQueuePresentKHR(app->present_queue, &present);
    app->frame = (app->frame + 1) % MAX_FRAMES;
}

int main(int argc, char **argv) {
    App app;
    memset(&app, 0, sizeof(app));
    editor_init(&app.editor, argc > 1 ? argv[1] : NULL);
    vk_init(&app);
    while (!glfwWindowShouldClose(app.vk.window)) {
        glfwPollEvents();
        draw_frame(&app);
    }
    vkDeviceWaitIdle(app.vk.device);
    return 0;
}
