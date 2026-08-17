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
    t->line_cap = 128;
    t->line_count = 1;
    t->line_starts = xmalloc(t->line_cap * sizeof(*t->line_starts));
    t->line_starts[0] = 0;
}

void text_free(Text *t) {
    free(t->data);
    free(t->line_starts);
    memset(t, 0, sizeof(*t));
}

void text_reserve(Text *t, size_t cap) {
    if (cap <= t->cap) return;
    while (t->cap < cap) t->cap *= 2;
    t->data = realloc(t->data, t->cap);
    if (!t->data) die("out of memory");
}

static void text_reserve_lines(Text *t, size_t cap) {
    if (cap <= t->line_cap) return;
    while (t->line_cap < cap) t->line_cap *= 2;
    t->line_starts = realloc(t->line_starts, t->line_cap * sizeof(*t->line_starts));
    if (!t->line_starts) die("out of memory");
}

static void text_rebuild_lines(Text *t) {
    size_t lines = 1;
    for (size_t i = 0; i < t->len; i++) {
        if (t->data[i] == '\n') lines++;
    }
    text_reserve_lines(t, lines);
    t->line_count = 1;
    t->line_starts[0] = 0;
    for (size_t i = 0; i < t->len; i++) {
        if (t->data[i] == '\n') t->line_starts[t->line_count++] = i + 1;
    }
}

static size_t text_line_index_at_pos(const Text *t, size_t pos) {
    if (pos > t->len) pos = t->len;
    size_t lo = 0, hi = t->line_count;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (t->line_starts[mid] <= pos) lo = mid;
        else hi = mid;
    }
    return lo;
}

static void text_lines_inserted(Text *t, size_t pos, const char *s, size_t n) {
    size_t newlines = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\n') newlines++;
    }
    if (newlines == 0) {
        size_t line = text_line_index_at_pos(t, pos);
        for (size_t i = line + 1; i < t->line_count; i++) t->line_starts[i] += n;
        return;
    }
    size_t line = text_line_index_at_pos(t, pos);
    text_reserve_lines(t, t->line_count + newlines);
    memmove(t->line_starts + line + 1 + newlines, t->line_starts + line + 1,
            (t->line_count - line - 1) * sizeof(*t->line_starts));
    size_t out = line + 1;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\n') t->line_starts[out++] = pos + i + 1;
    }
    t->line_count += newlines;
    for (size_t i = line + 1 + newlines; i < t->line_count; i++) t->line_starts[i] += n;
}

static void text_lines_deleted(Text *t, size_t pos, size_t n) {
    size_t end = pos + n;
    size_t first = text_line_index_at_pos(t, pos);
    size_t remove_first = first + 1;
    while (remove_first < t->line_count && t->line_starts[remove_first] <= pos) remove_first++;
    size_t remove_end = remove_first;
    while (remove_end < t->line_count && t->line_starts[remove_end] <= end) remove_end++;
    size_t removed = remove_end - remove_first;
    if (removed) {
        memmove(t->line_starts + remove_first, t->line_starts + remove_end,
                (t->line_count - remove_end) * sizeof(*t->line_starts));
        t->line_count -= removed;
    }
    for (size_t i = remove_first; i < t->line_count; i++) t->line_starts[i] -= n;
}

void text_set(Text *t, const char *s, size_t n) {
    text_reserve(t, n + 1);
    memcpy(t->data, s, n);
    t->data[n] = 0;
    t->len = n;
    text_rebuild_lines(t);
}

void text_insert(Text *t, size_t pos, const char *s, size_t n) {
    if (pos > t->len) pos = t->len;
    text_reserve(t, t->len + n + 1);
    memmove(t->data + pos + n, t->data + pos, t->len - pos + 1);
    memcpy(t->data + pos, s, n);
    text_lines_inserted(t, pos, s, n);
    t->len += n;
}

void text_delete(Text *t, size_t pos, size_t n) {
    if (pos >= t->len) return;
    if (pos + n > t->len) n = t->len - pos;
    text_lines_deleted(t, pos, n);
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
    if (text->len > MED_UNDO_SNAPSHOT_MAX_BYTES) return;
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
    return t->line_starts[text_line_index_at_pos(t, pos)];
}

size_t line_end_at(const Text *t, size_t pos) {
    if (pos > t->len) pos = t->len;
    while (pos < t->len && t->data[pos] != '\n') pos++;
    return pos;
}

int byte_line(const Text *t, size_t pos) {
    return (int)text_line_index_at_pos(t, pos);
}

int byte_col(const Text *t, size_t pos) {
    return (int)(pos - line_start_at(t, pos));
}

size_t line_start_by_number(const Text *t, int line) {
    if (line <= 0) return 0;
    if ((size_t)line >= t->line_count) return t->len;
    return t->line_starts[line];
}

int line_count(const Text *t) {
    return (int)t->line_count;
}

size_t clamp_cursor_for_normal(const Text *t, size_t pos) {
    size_t start = line_start_at(t, pos);
    size_t end = line_end_at(t, pos);
    if (end > start && pos >= end) return end - 1;
    return pos;
}
