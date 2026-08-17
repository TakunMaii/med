#include "med.h"

const Theme gruvbox = {
    .bg = {0.157f, 0.157f, 0.157f, 1.0f},
    .fg = {0.922f, 0.859f, 0.698f, 1.0f},
    .gutter_bg = {0.157f, 0.157f, 0.157f, 1.0f},
    .gutter_fg = {0.573f, 0.514f, 0.455f, 1.0f},
    .line_no_current = {0.980f, 0.741f, 0.184f, 1.0f},
    .cursor = {0.922f, 0.859f, 0.698f, 0.38f},
    .cursor_insert = {0.980f, 0.741f, 0.184f, 1.0f},
    .selection = {0.271f, 0.522f, 0.533f, 0.45f},
    .search_match = {0.980f, 0.741f, 0.184f, 0.28f},
    .keyword = {0.984f, 0.286f, 0.204f, 1.0f},
    .string = {0.722f, 0.733f, 0.149f, 1.0f},
    .comment = {0.573f, 0.514f, 0.455f, 1.0f},
    .function = {0.514f, 0.647f, 0.596f, 1.0f},
    .type = {0.980f, 0.741f, 0.184f, 1.0f},
    .number = {0.827f, 0.525f, 0.608f, 1.0f},
    .preproc = {0.847f, 0.600f, 0.129f, 1.0f},
};

void die(const char *msg) {
    fprintf(stderr, "med: %s\n", msg);
    exit(1);
}

void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) die("out of memory");
    return p;
}

char *read_file(const char *path, size_t *len_out) {
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

bool write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    return ok;
}

void text_init(Text *t) {
    t->cap = 4096;
    t->len = 0;
    t->data = xmalloc(t->cap);
    t->data[0] = 0;
}

void text_reserve(Text *t, size_t cap) {
    if (cap <= t->cap) return;
    while (t->cap < cap) t->cap *= 2;
    t->data = realloc(t->data, t->cap);
    if (!t->data) die("out of memory");
}

void text_set(Text *t, const char *s, size_t n) {
    text_reserve(t, n + 1);
    memcpy(t->data, s, n);
    t->data[n] = 0;
    t->len = n;
}

void text_insert(Text *t, size_t pos, const char *s, size_t n) {
    if (pos > t->len) pos = t->len;
    text_reserve(t, t->len + n + 1);
    memmove(t->data + pos + n, t->data + pos, t->len - pos + 1);
    memcpy(t->data + pos, s, n);
    t->len += n;
}

void text_delete(Text *t, size_t pos, size_t n) {
    if (pos >= t->len) return;
    if (pos + n > t->len) n = t->len - pos;
    memmove(t->data + pos, t->data + pos + n, t->len - pos - n + 1);
    t->len -= n;
}

void snapshot_free(Snapshot *s) {
    free(s->data);
    s->data = NULL;
    s->len = 0;
    s->cursor = 0;
}

void snapshot_stack_clear(SnapshotStack *st) {
    for (size_t i = 0; i < st->len; i++) snapshot_free(&st->items[i]);
    st->len = 0;
}

void snapshot_stack_push(SnapshotStack *st, const Text *text, size_t cursor) {
    if (st->len == st->cap) {
        st->cap = st->cap ? st->cap * 2 : 64;
        st->items = realloc(st->items, st->cap * sizeof(*st->items));
        if (!st->items) die("out of memory");
    }
    Snapshot *s = &st->items[st->len++];
    s->data = xmalloc(text->len + 1);
    memcpy(s->data, text->data, text->len + 1);
    s->len = text->len;
    s->cursor = cursor;
}

bool snapshot_stack_pop(SnapshotStack *st, Snapshot *out) {
    if (st->len == 0) return false;
    *out = st->items[--st->len];
    memset(&st->items[st->len], 0, sizeof(st->items[st->len]));
    return true;
}

size_t line_start_at(const Text *t, size_t pos) {
    if (pos > t->len) pos = t->len;
    while (pos > 0 && t->data[pos - 1] != '\n') pos--;
    return pos;
}

size_t line_end_at(const Text *t, size_t pos) {
    if (pos > t->len) pos = t->len;
    while (pos < t->len && t->data[pos] != '\n') pos++;
    return pos;
}

int byte_line(const Text *t, size_t pos) {
    int line = 0;
    if (pos > t->len) pos = t->len;
    for (size_t i = 0; i < pos; i++) {
        if (t->data[i] == '\n') line++;
    }
    return line;
}

int byte_col(const Text *t, size_t pos) {
    return (int)(pos - line_start_at(t, pos));
}

size_t line_start_by_number(const Text *t, int line) {
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

int line_count(const Text *t) {
    int lines = 1;
    for (size_t i = 0; i < t->len; i++) {
        if (t->data[i] == '\n') lines++;
    }
    return lines;
}

size_t clamp_cursor_for_normal(const Text *t, size_t pos) {
    size_t start = line_start_at(t, pos);
    size_t end = line_end_at(t, pos);
    if (end > start && pos >= end) return end - 1;
    return pos;
}
