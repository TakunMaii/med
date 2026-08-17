#ifndef MED_H
#define MED_H

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
#include <stddef.h>
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
#define CURSOR_TRAIL_MAX 18
#define CURSOR_TRAIL_SECONDS 0.28
#define CURSOR_ANIM_STIFFNESS 95.0f
#define CURSOR_ANIM_DAMPING 0.82f
#define CURSOR_MAX_TRAIL_CELLS 18.0f
#define CURSOR_SEGMENT_MAX 4
#define EDITOR_MAX_TABS 16
#define EDITOR_MAX_VIEWS 32
#define EDITOR_MAX_SPLIT_NODES 63
#define MED_PARSE_MAX_BYTES (2u * 1024u * 1024u)
#define MED_UNDO_SNAPSHOT_MAX_BYTES (2u * 1024u * 1024u)

typedef struct {
    float r, g, b, a;
} Color;

typedef struct {
    Color bg, fg, gutter_bg, gutter_fg, line_no_current;
    Color cursor, cursor_insert, selection, search_match;
    Color keyword, string, comment, function, type, number, preproc;
} Theme;

extern const Theme gruvbox;

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
    size_t *line_starts;
    size_t line_count;
    size_t line_cap;
} Text;

typedef struct {
    char *data;
    size_t len;
    size_t cursor;
} Snapshot;

typedef struct {
    Snapshot *items;
    size_t len, cap;
} SnapshotStack;

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
    int line;
    int col;
    Mode mode;
    double time;
} CursorTrail;

typedef struct {
    float x;
    float y;
    float velocity_x;
    float velocity_y;
    double last_time;
    bool initialized;
} AnimatedCursor;

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
    SnapshotStack undo;
    SnapshotStack redo;
} BufferSlot;

typedef struct {
    size_t buffer_index;
    size_t cursor;
    size_t visual_anchor;
    int desired_col;
    int top_line;
    int left_col;
    AnimatedCursor cursor_anim;
} EditorView;

typedef enum { SPLIT_LEAF, SPLIT_ROW, SPLIT_COL } SplitKind;

typedef struct {
    SplitKind kind;
    float ratio;
    int first;
    int second;
    size_t view_index;
} SplitNode;

typedef struct {
    EditorView views[EDITOR_MAX_VIEWS];
    size_t view_count;
    size_t active_view;
    SplitNode nodes[EDITOR_MAX_SPLIT_NODES];
    size_t node_count;
    int root;
} EditorTab;

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
    char operator_pending;
    int count;
    int operator_count;
    char waiting_char;
    char last_find_char;
    char last_find_cmd;
    bool visual_line;
    bool visual_block;
    bool suppress_next_char;
    char command[256];
    size_t command_len;
    char status[512];
    char search[256];
    size_t search_len;
    bool search_backward;
    bool search_active;
    bool show_number;
    bool relative_number;
    int last_search_dir;
    char last_change[128];
    Text yank;
    bool has_yank;
    bool yank_linewise;
    bool yank_blockwise;
    SnapshotStack undo;
    SnapshotStack redo;
    CursorTrail cursor_trail[CURSOR_TRAIL_MAX];
    size_t cursor_trail_len;
    AnimatedCursor cursor_anim;
    TSLanguage *c_lang;
    TSParser *parser;
    TSQuery *query;
    TSTree *tree;
    Highlights highlights;
    BufferSlot *buffers;
    size_t buffer_count;
    size_t current_buffer;
    EditorTab tabs[EDITOR_MAX_TABS];
    size_t tab_count;
    size_t current_tab;
    char window_pending;
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
    float rect_min[2];
    float rect_max[2];
    float p0[2];
    float p1[2];
    float half_size[2];
    float color[4];
    float softness;
    float intensity;
    float mode;
    float _pad;
} CursorSegment;

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
    VkPipelineLayout cursor_pipeline_layout;
    VkPipeline cursor_pipeline;
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
    CursorSegment cursor_segments[CURSOR_SEGMENT_MAX];
    uint32_t cursor_segment_count;
} VkApp;

typedef struct {
    Editor editor;
    VkApp vk;
} App;

void die(const char *msg);
void *xmalloc(size_t n);
char *read_file(const char *path, size_t *len_out);
bool write_file(const char *path, const char *data, size_t len);

void text_init(Text *t);
void text_free(Text *t);
void text_reserve(Text *t, size_t cap);
void text_set(Text *t, const char *s, size_t n);
void text_insert(Text *t, size_t pos, const char *s, size_t n);
void text_delete(Text *t, size_t pos, size_t n);

void snapshot_free(Snapshot *s);
void snapshot_stack_clear(SnapshotStack *st);
void snapshot_stack_push(SnapshotStack *st, const Text *text, size_t cursor);
bool snapshot_stack_pop(SnapshotStack *st, Snapshot *out);

size_t line_start_at(const Text *t, size_t pos);
size_t line_end_at(const Text *t, size_t pos);
int byte_line(const Text *t, size_t pos);
int byte_col(const Text *t, size_t pos);
size_t line_start_by_number(const Text *t, int line);
int line_count(const Text *t);
size_t clamp_cursor_for_normal(const Text *t, size_t pos);

void editor_init(Editor *e, const char *path);
bool editor_open_buffer(Editor *e, const char *path);
void editor_load_buffer(Editor *e, size_t index);
void editor_store_current_buffer(Editor *e);
void editor_reparse(Editor *e);
void editor_ensure_visible(Editor *e, int rows, int cols);
void editor_record_cursor_if_moved(Editor *e, int old_line, int old_col, Mode old_mode, double now);
void editor_key(Editor *e, int key, int mods, int rows);
void editor_handle_waiting_char(Editor *e, char ch);
void editor_insert_char(Editor *e, unsigned int cp);
HighlightKind highlight_at(const Editor *e, size_t byte);
void app_execute_command(App *app);
void editor_sync_active_view(Editor *e);
void editor_split_current(Editor *e, bool vertical, const char *path);
void editor_close_view(Editor *e, bool only);
void editor_focus_view_direction(Editor *e, char dir);
void editor_tab_new(Editor *e, const char *path);
void editor_tab_switch(Editor *e, int delta);
void editor_tab_close(Editor *e);

void vk_init(App *owner);
void draw_frame(App *owner);

#endif
