#include "med.h"
#include "keymap.h"

static void editor_set_status(Editor *e, const char *msg);

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

static bool editor_event_changes_text(ReplayEvent ev) {
    if (ev.kind == REPLAY_CHAR) return true;
    if (ev.kind == REPLAY_WAITING_CHAR) return true;
    if (ev.kind != REPLAY_KEY) return false;
    if (ev.key == GLFW_KEY_ESCAPE) return true;
    if (ev.key == GLFW_KEY_ENTER || ev.key == GLFW_KEY_BACKSPACE) return true;
    if (ev.key == GLFW_KEY_U && !(ev.mods & GLFW_MOD_CONTROL)) return false;
    return true;
}

static void replay_buffer_push(ReplayEvent *events, size_t *len, size_t cap, ReplayEvent ev) {
    if (*len >= cap) return;
    events[(*len)++] = ev;
}

static void editor_macro_record_event(Editor *e, ReplayEvent ev) {
    if (!e->macro_recording || e->replaying) return;
    if (e->macro_record_register < 'a' || e->macro_record_register > 'z') return;
    MacroRegister *m = &e->macros[e->macro_record_register - 'a'];
    replay_buffer_push(m->events, &m->len, EDITOR_MACRO_MAX_EVENTS, ev);
}

static void editor_begin_recorded_change(Editor *e) {
    if (e->suppress_change_record || e->replaying) return;
    e->building_change = true;
    e->current_change_event_len = 0;
}

static void editor_record_change_event(Editor *e, ReplayEvent ev) {
    if (e->suppress_change_record || e->replaying || !e->building_change) return;
    replay_buffer_push(e->current_change_events, &e->current_change_event_len, EDITOR_REPLAY_MAX_EVENTS, ev);
}

static void editor_commit_recorded_change(Editor *e) {
    if (e->suppress_change_record || e->replaying || !e->building_change) return;
    if (e->current_change_event_len > 0) {
        memcpy(e->last_change_events, e->current_change_events, e->current_change_event_len * sizeof(e->current_change_events[0]));
        e->last_change_event_len = e->current_change_event_len;
    }
    e->building_change = false;
    e->current_change_event_len = 0;
}

static void editor_cancel_recorded_change(Editor *e) {
    e->building_change = false;
    e->current_change_event_len = 0;
}

static bool editor_execute_event(Editor *e, ReplayEvent ev, int rows);

static void editor_replay_events(Editor *e, const ReplayEvent *events, size_t len, int rows) {
    if (len == 0 || e->macro_replay_depth > 8) return;
    e->macro_replay_depth++;
    bool old_replaying = e->replaying;
    e->replaying = true;
    for (size_t i = 0; i < len; i++) {
        if (++e->macro_replay_steps > EDITOR_MACRO_REPLAY_LIMIT) {
            editor_set_status(e, "Replay stopped");
            break;
        }
        editor_execute_event(e, events[i], rows);
    }
    e->replaying = old_replaying;
    e->macro_replay_depth--;
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

static bool is_space_byte(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool is_punct_word_byte(char c) {
    return c != 0 && !is_space_byte(c) && !is_word_byte(c);
}

static bool is_big_word_byte(char c) {
    return c != 0 && !is_space_byte(c);
}

static void editor_move_line_end(Editor *e) {
    e->cursor = line_end_at(&e->text, e->cursor);
    if (e->mode != MODE_INSERT) e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
    e->desired_col = byte_col(&e->text, e->cursor);
}

static size_t first_nonblank_on_line(const Text *t, size_t pos) {
    size_t p = line_start_at(t, pos);
    size_t end = line_end_at(t, p);
    while (p < end && (t->data[p] == ' ' || t->data[p] == '\t')) p++;
    return p;
}

static size_t next_line_first_nonblank(const Text *t, size_t pos, int delta) {
    int line = byte_line(t, pos) + delta;
    if (line < 0) line = 0;
    int lc = line_count(t);
    if (line >= lc) line = lc - 1;
    return first_nonblank_on_line(t, line_start_by_number(t, line));
}

static size_t word_forward_pos(const Text *t, size_t cursor, int count) {
    size_t p = cursor;
    for (int n = 0; n < count; n++) {
        if (p < t->len) p++;
        if (p > 0 && is_word_byte(t->data[p - 1])) {
            while (p < t->len && is_word_byte(t->data[p])) p++;
        } else if (p > 0 && is_punct_word_byte(t->data[p - 1])) {
            while (p < t->len && is_punct_word_byte(t->data[p])) p++;
        }
        while (p < t->len && is_space_byte(t->data[p])) p++;
    }
    return clamp_cursor_for_normal(t, p);
}

static size_t word_back_pos(const Text *t, size_t cursor, int count) {
    size_t p = cursor;
    for (int n = 0; n < count; n++) {
        if (p == 0) break;
        p--;
        while (p > 0 && is_space_byte(t->data[p])) p--;
        if (is_word_byte(t->data[p])) {
            while (p > 0 && is_word_byte(t->data[p - 1])) p--;
        } else if (is_punct_word_byte(t->data[p])) {
            while (p > 0 && is_punct_word_byte(t->data[p - 1])) p--;
        }
    }
    return p;
}

static size_t word_end_pos(const Text *t, size_t cursor, int count) {
    size_t p = cursor;
    for (int n = 0; n < count; n++) {
        if (p < t->len) p++;
        while (p < t->len && is_space_byte(t->data[p])) p++;
        if (p < t->len && is_word_byte(t->data[p])) {
            while (p + 1 < t->len && is_word_byte(t->data[p + 1])) p++;
        } else if (p < t->len && is_punct_word_byte(t->data[p])) {
            while (p + 1 < t->len && is_punct_word_byte(t->data[p + 1])) p++;
        }
    }
    return clamp_cursor_for_normal(t, p);
}

static size_t big_word_forward_pos(const Text *t, size_t cursor, int count) {
    size_t p = cursor;
    for (int n = 0; n < count; n++) {
        if (p < t->len) p++;
        while (p < t->len && is_big_word_byte(t->data[p])) p++;
        while (p < t->len && is_space_byte(t->data[p])) p++;
    }
    return clamp_cursor_for_normal(t, p);
}

static size_t big_word_back_pos(const Text *t, size_t cursor, int count) {
    size_t p = cursor;
    for (int n = 0; n < count; n++) {
        if (p == 0) break;
        p--;
        while (p > 0 && is_space_byte(t->data[p])) p--;
        while (p > 0 && is_big_word_byte(t->data[p - 1])) p--;
    }
    return p;
}

static size_t big_word_end_pos(const Text *t, size_t cursor, int count) {
    size_t p = cursor;
    for (int n = 0; n < count; n++) {
        if (p < t->len) p++;
        while (p < t->len && is_space_byte(t->data[p])) p++;
        while (p + 1 < t->len && is_big_word_byte(t->data[p + 1])) p++;
    }
    return clamp_cursor_for_normal(t, p);
}

static size_t word_prev_end_pos(const Text *t, size_t cursor, int count, bool big) {
    size_t p = cursor;
    for (int n = 0; n < count; n++) {
        if (p == 0) break;
        p--;
        while (p > 0 && is_space_byte(t->data[p])) p--;
        if (big) {
            while (p + 1 < t->len && is_big_word_byte(t->data[p + 1])) p++;
        } else if (is_word_byte(t->data[p])) {
            while (p + 1 < t->len && is_word_byte(t->data[p + 1])) p++;
        } else if (is_punct_word_byte(t->data[p])) {
            while (p + 1 < t->len && is_punct_word_byte(t->data[p + 1])) p++;
        }
        if (n + 1 < count) {
            if (p == 0) break;
            p = big ? big_word_back_pos(t, p, 1) : word_back_pos(t, p, 1);
        }
    }
    return clamp_cursor_for_normal(t, p);
}

static bool line_is_blank(const Text *t, int line) {
    size_t p = line_start_by_number(t, line);
    size_t end = line_end_at(t, p);
    while (p < end) {
        if (t->data[p] != ' ' && t->data[p] != '\t') return false;
        p++;
    }
    return true;
}

static size_t paragraph_pos(const Text *t, size_t cursor, int count, int dir) {
    int line = byte_line(t, cursor);
    int lc = line_count(t);
    for (int n = 0; n < count; n++) {
        if (dir > 0) {
            if (line < lc - 1) line++;
            while (line < lc - 1 && !line_is_blank(t, line)) line++;
            while (line < lc - 1 && line_is_blank(t, line)) line++;
        } else {
            if (line > 0) line--;
            while (line > 0 && line_is_blank(t, line)) line--;
            while (line > 0 && !line_is_blank(t, line - 1)) line--;
        }
    }
    return first_nonblank_on_line(t, line_start_by_number(t, line));
}

static size_t line_last_nonblank(const Text *t, size_t pos) {
    size_t start = line_start_at(t, pos);
    size_t end = line_end_at(t, pos);
    while (end > start && (t->data[end - 1] == ' ' || t->data[end - 1] == '\t')) end--;
    return end > start ? end - 1 : start;
}

static void editor_go_to(Editor *e, size_t pos) {
    e->cursor = clamp_cursor_for_normal(&e->text, pos);
    e->desired_col = byte_col(&e->text, e->cursor);
}

static void editor_scroll_cursor_to_fraction(Editor *e, int rows, int which) {
    int line = byte_line(&e->text, e->cursor);
    if (which == 0) e->top_line = line;
    else if (which == 1) e->top_line = line - rows / 2;
    else e->top_line = line - rows + 1;
    if (e->top_line < 0) e->top_line = 0;
}

static bool matching_pair(char c, char *other, int *dir) {
    switch (c) {
    case '(': *other = ')'; *dir = 1; return true;
    case '[': *other = ']'; *dir = 1; return true;
    case '{': *other = '}'; *dir = 1; return true;
    case ')': *other = '('; *dir = -1; return true;
    case ']': *other = '['; *dir = -1; return true;
    case '}': *other = '{'; *dir = -1; return true;
    default: return false;
    }
}

static bool editor_find_matching_pair(Editor *e, size_t *out) {
    size_t p = e->cursor;
    while (p < e->text.len && !strchr("()[]{}", e->text.data[p])) {
        if (e->text.data[p] == '\n') return false;
        p++;
    }
    if (p >= e->text.len) return false;
    char open = e->text.data[p], close = 0;
    int dir = 0, depth = 0;
    if (!matching_pair(open, &close, &dir)) return false;
    for (size_t i = p;;) {
        char c = e->text.data[i];
        if (c == open) depth++;
        if (c == close) {
            depth--;
            if (depth == 0) {
                *out = i;
                return true;
            }
        }
        if (dir > 0) {
            if (++i >= e->text.len) break;
        } else {
            if (i == 0) break;
            i--;
        }
    }
    return false;
}

static bool editor_find_char_on_line(Editor *e, char target, char cmd, int count, size_t *out) {
    size_t p = e->cursor;
    size_t start = line_start_at(&e->text, p);
    size_t end = line_end_at(&e->text, p);
    int dir = (cmd == 'f' || cmd == 't') ? 1 : -1;
    int seen = 0;
    if (dir > 0) {
        for (size_t i = p + 1; i < end; i++) {
            if (e->text.data[i] == target && ++seen == count) {
                *out = (cmd == 't' && i > start) ? i - 1 : i;
                return true;
            }
        }
    } else {
        for (size_t i = p; i > start; i--) {
            size_t j = i - 1;
            if (e->text.data[j] == target && ++seen == count) {
                *out = (cmd == 'T' && j + 1 < end) ? j + 1 : j;
                return true;
            }
        }
    }
    return false;
}

static bool editor_search(Editor *e, const char *needle, int dir, size_t *out) {
    size_t n = strlen(needle);
    if (n == 0 || n > e->text.len) return false;
    if (dir >= 0) {
        size_t start = e->cursor + 1 < e->text.len ? e->cursor + 1 : 0;
        for (size_t i = start; i + n <= e->text.len; i++) {
            if (memcmp(e->text.data + i, needle, n) == 0) {
                *out = i;
                return true;
            }
        }
        for (size_t i = 0; i < start && i + n <= e->text.len; i++) {
            if (memcmp(e->text.data + i, needle, n) == 0) {
                *out = i;
                return true;
            }
        }
    } else {
        size_t start = e->cursor > 0 ? e->cursor - 1 : e->text.len;
        if (start + n > e->text.len) start = e->text.len - n;
        for (size_t i = start + 1; i-- > 0;) {
            if (i + n <= e->text.len && memcmp(e->text.data + i, needle, n) == 0) {
                *out = i;
                return true;
            }
            if (i == 0) break;
        }
        for (size_t i = e->text.len - n + 1; i-- > start + 1;) {
            if (i + n <= e->text.len && memcmp(e->text.data + i, needle, n) == 0) {
                *out = i;
                return true;
            }
            if (i == 0) break;
        }
    }
    return false;
}

static void editor_set_status(Editor *e, const char *msg);

void editor_repeat_search(Editor *e, int dir) {
    if (!e->search_active || e->search[0] == 0) return;
    size_t pos = 0;
    if (editor_search(e, e->search, dir, &pos)) {
        editor_go_to(e, pos);
        e->last_search_dir = dir;
    } else {
        editor_set_status(e, "Pattern not found");
    }
}

static void editor_set_status(Editor *e, const char *msg) {
    snprintf(e->status, sizeof(e->status), "%s", msg ? msg : "");
}

static void editor_mark_text_changed(Editor *e) {
    e->dirty = true;
    e->lsp.needs_sync = true;
}

void editor_begin_change(Editor *e) {
    snapshot_stack_push(&e->undo, &e->text, e->cursor);
    snapshot_stack_clear(&e->redo);
    editor_mark_text_changed(e);
}

static void editor_begin_insert_change(Editor *e) {
    if (!e->insert_change_active) {
        editor_begin_change(e);
        e->insert_change_active = true;
        e->insert_change_had_edit = true;
        e->last_insert_len = 0;
        e->last_insert[0] = 0;
    }
}

static void editor_continue_insert_change(Editor *e) {
    e->insert_change_active = true;
    e->insert_change_had_edit = true;
    e->last_insert_len = 0;
    e->last_insert[0] = 0;
}

static void editor_finish_insert_change(Editor *e) {
    if (e->insert_change_active && e->insert_change_had_edit && e->last_insert_len > 0) {
        snprintf(e->last_change, sizeof(e->last_change), "insert");
    }
    e->insert_change_active = false;
    e->insert_change_had_edit = false;
}

static void editor_record_insert_text(Editor *e, const char *s, size_t n) {
    if (n == 0 || e->last_insert_len >= sizeof(e->last_insert) - 1) return;
    size_t room = sizeof(e->last_insert) - 1 - e->last_insert_len;
    if (n > room) n = room;
    memcpy(e->last_insert + e->last_insert_len, s, n);
    e->last_insert_len += n;
    e->last_insert[e->last_insert_len] = 0;
}

static void editor_replace_with_snapshot(Editor *e, Snapshot s) {
    text_set(&e->text, s.data, s.len);
    e->cursor = s.cursor <= e->text.len ? s.cursor : e->text.len;
    e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
    e->desired_col = byte_col(&e->text, e->cursor);
    e->dirty = true;
    editor_reparse(e);
    snapshot_free(&s);
}

static void editor_undo(Editor *e) {
    Snapshot s;
    if (!snapshot_stack_pop(&e->undo, &s)) {
        editor_set_status(e, "Already at oldest change");
        return;
    }
    snapshot_stack_push(&e->redo, &e->text, e->cursor);
    editor_replace_with_snapshot(e, s);
}

static void editor_redo(Editor *e) {
    Snapshot s;
    if (!snapshot_stack_pop(&e->redo, &s)) {
        editor_set_status(e, "Already at newest change");
        return;
    }
    snapshot_stack_push(&e->undo, &e->text, e->cursor);
    editor_replace_with_snapshot(e, s);
}

void editor_store_current_buffer(Editor *e) {
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
    b->undo = e->undo;
    b->redo = e->redo;
}

static void editor_apply_buffer_slot(Editor *e, size_t index) {
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
    e->undo = b->undo;
    e->redo = b->redo;
}

static EditorTab *editor_active_tab(Editor *e) {
    if (e->tab_count == 0) return NULL;
    if (e->current_tab >= e->tab_count) e->current_tab = e->tab_count - 1;
    return &e->tabs[e->current_tab];
}

static EditorView *editor_active_view(Editor *e) {
    EditorTab *tab = editor_active_tab(e);
    if (!tab || tab->view_count == 0) return NULL;
    if (tab->active_view >= tab->view_count) tab->active_view = tab->view_count - 1;
    return &tab->views[tab->active_view];
}

void editor_sync_active_view(Editor *e) {
    EditorView *v = editor_active_view(e);
    if (!v) return;
    v->buffer_index = e->current_buffer;
    v->cursor = e->cursor;
    v->visual_anchor = e->visual_anchor;
    v->desired_col = e->desired_col;
    v->top_line = e->top_line;
    v->left_col = e->left_col;
    v->cursor_anim = e->cursor_anim;
}

static void editor_load_view_state(Editor *e, size_t view_index, bool save_current) {
    EditorTab *tab = editor_active_tab(e);
    if (!tab || view_index >= tab->view_count) return;
    if (save_current) {
        editor_store_current_buffer(e);
        editor_sync_active_view(e);
    }
    tab->active_view = view_index;
    EditorView *v = &tab->views[view_index];
    if (v->buffer_index >= e->buffer_count) v->buffer_index = 0;
    editor_apply_buffer_slot(e, v->buffer_index);
    if (!e->tree) {
        editor_reparse(e);
        editor_store_current_buffer(e);
    }
    e->cursor = v->cursor <= e->text.len ? v->cursor : e->text.len;
    e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
    e->visual_anchor = v->visual_anchor <= e->text.len ? v->visual_anchor : e->cursor;
    e->desired_col = v->desired_col;
    e->top_line = v->top_line;
    e->left_col = v->left_col;
    e->cursor_anim = v->cursor_anim;
    e->mode = MODE_NORMAL;
    e->pending = 0;
    e->operator_pending = 0;
    e->count = 0;
    e->operator_count = 0;
    e->waiting_char = 0;
    e->window_pending = 0;
    e->cursor_trail_len = 0;
}

static void editor_load_view(Editor *e, size_t view_index) {
    editor_load_view_state(e, view_index, true);
}

void editor_load_buffer(Editor *e, size_t index) {
    if (!e->buffers || index >= e->buffer_count) return;
    editor_store_current_buffer(e);
    editor_apply_buffer_slot(e, index);
    if (!e->tree) editor_reparse(e);
    e->mode = MODE_NORMAL;
    e->pending = 0;
    e->operator_pending = 0;
    e->count = 0;
    e->operator_count = 0;
    e->waiting_char = 0;
    e->cursor_trail_len = 0;
    e->cursor_anim.initialized = false;
    EditorView *v = editor_active_view(e);
    if (v) {
        v->buffer_index = index;
        v->cursor = e->cursor;
        v->visual_anchor = e->visual_anchor;
        v->desired_col = e->desired_col;
        v->top_line = e->top_line;
        v->left_col = e->left_col;
        v->cursor_anim = e->cursor_anim;
    }
}

void editor_add_buffer(Editor *e, const char *path, const char *content, size_t len) {
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

static bool editor_open_buffer_index(Editor *e, const char *path, size_t *out) {
    size_t n = 0;
    char *data = read_file(path, &n);
    if (!data) return false;
    editor_add_buffer(e, path, data, n);
    free(data);
    *out = e->buffer_count - 1;
    return true;
}

bool editor_open_buffer(Editor *e, const char *path) {
    size_t index = 0;
    editor_store_current_buffer(e);
    if (!editor_open_buffer_index(e, path, &index)) return false;
    editor_load_buffer(e, index);
    editor_reparse(e);
    lsp_maybe_start(e);
    editor_store_current_buffer(e);
    return true;
}

void editor_switch_relative_buffer(Editor *e, int delta) {
    if (e->buffer_count == 0) return;
    size_t next = (e->current_buffer + e->buffer_count + (size_t)(delta > 0 ? 1 : e->buffer_count - 1)) % e->buffer_count;
    editor_load_buffer(e, next);
}

bool editor_save_current(Editor *e, const char *path) {
    const char *target = path && path[0] ? path : e->path;
    if (!target[0]) return false;
    if (!write_file(target, e->text.data, e->text.len)) return false;
    snprintf(e->path, sizeof(e->path), "%s", target);
    e->dirty = false;
    editor_store_current_buffer(e);
    return true;
}

bool editor_delete_current_buffer(Editor *e, bool force) {
    if (e->buffer_count <= 1) {
        editor_set_status(e, "Cannot delete the last buffer");
        return false;
    }
    if (e->dirty && !force) {
        editor_set_status(e, "No write since last change");
        return false;
    }
    size_t old = e->current_buffer;
    if (e->tree) ts_tree_delete(e->tree);
    text_free(&e->text);
    free(e->highlights.spans);
    snapshot_stack_clear(&e->undo);
    snapshot_stack_clear(&e->redo);
    free(e->undo.items);
    free(e->redo.items);
    memmove(&e->buffers[old], &e->buffers[old + 1], (e->buffer_count - old - 1) * sizeof(*e->buffers));
    e->buffer_count--;
    if (old >= e->buffer_count) old = e->buffer_count - 1;
    e->current_buffer = old;
    BufferSlot *b = &e->buffers[old];
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
    e->undo = b->undo;
    e->redo = b->redo;
    e->mode = MODE_NORMAL;
    return true;
}

void editor_init(Editor *e, const char *path) {
    memset(e, 0, sizeof(*e));
    text_init(&e->text);
    text_init(&e->yank);
    lsp_init(&e->lsp);
    for (int i = 0; i < 26; i++) text_init(&e->registers[i]);
    e->mode = MODE_NORMAL;
    e->desired_col = -1;
    e->show_number = true;
    e->relative_number = true;
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
    lsp_maybe_start(e);
    e->buffers = calloc(1, sizeof(*e->buffers));
    if (!e->buffers) die("out of memory");
    e->buffer_count = 1;
    e->current_buffer = 0;
    editor_store_current_buffer(e);
    e->tab_count = 1;
    e->current_tab = 0;
    EditorTab *tab = &e->tabs[0];
    memset(tab, 0, sizeof(*tab));
    tab->view_count = 1;
    tab->active_view = 0;
    tab->views[0] = (EditorView){
        .buffer_index = 0,
        .cursor = e->cursor,
        .visual_anchor = e->visual_anchor,
        .desired_col = e->desired_col,
        .top_line = e->top_line,
        .left_col = e->left_col,
    };
    tab->node_count = 1;
    tab->root = 0;
    tab->nodes[0] = (SplitNode){.kind = SPLIT_LEAF, .ratio = 0.5f, .first = -1, .second = -1, .view_index = 0};
}

static EditorView make_view_from_editor(Editor *e, size_t buffer_index) {
    return (EditorView){
        .buffer_index = buffer_index,
        .cursor = e->cursor,
        .visual_anchor = e->visual_anchor,
        .desired_col = e->desired_col,
        .top_line = e->top_line,
        .left_col = e->left_col,
    };
}

static int split_find_leaf_for_view(EditorTab *tab, int node_index, size_t view_index) {
    if (node_index < 0 || (size_t)node_index >= tab->node_count) return -1;
    SplitNode *n = &tab->nodes[node_index];
    if (n->kind == SPLIT_LEAF) return n->view_index == view_index ? node_index : -1;
    int found = split_find_leaf_for_view(tab, n->first, view_index);
    return found >= 0 ? found : split_find_leaf_for_view(tab, n->second, view_index);
}

static int split_find_parent(EditorTab *tab, int node_index, int child_index) {
    if (node_index < 0 || (size_t)node_index >= tab->node_count) return -1;
    SplitNode *n = &tab->nodes[node_index];
    if (n->kind == SPLIT_LEAF) return -1;
    if (n->first == child_index || n->second == child_index) return node_index;
    int found = split_find_parent(tab, n->first, child_index);
    return found >= 0 ? found : split_find_parent(tab, n->second, child_index);
}

static bool split_remove_leaf(EditorTab *tab, int leaf_index) {
    if (leaf_index < 0 || (size_t)leaf_index >= tab->node_count) return false;
    if (leaf_index == tab->root) return false;
    int parent_index = split_find_parent(tab, tab->root, leaf_index);
    if (parent_index < 0) return false;
    SplitNode *parent = &tab->nodes[parent_index];
    int sibling_index = parent->first == leaf_index ? parent->second : parent->first;
    parent->kind = tab->nodes[sibling_index].kind;
    parent->ratio = tab->nodes[sibling_index].ratio;
    parent->first = tab->nodes[sibling_index].first;
    parent->second = tab->nodes[sibling_index].second;
    parent->view_index = tab->nodes[sibling_index].view_index;
    return true;
}

static void tab_init_single_view(EditorTab *tab, size_t buffer_index, Editor *e) {
    memset(tab, 0, sizeof(*tab));
    tab->view_count = 1;
    tab->active_view = 0;
    tab->views[0] = make_view_from_editor(e, buffer_index);
    tab->nodes[0] = (SplitNode){.kind = SPLIT_LEAF, .ratio = 0.5f, .first = -1, .second = -1, .view_index = 0};
    tab->node_count = 1;
    tab->root = 0;
}

void editor_split_current(Editor *e, bool vertical, const char *path) {
    EditorTab *tab = editor_active_tab(e);
    if (!tab || tab->view_count >= EDITOR_MAX_VIEWS || tab->node_count + 2 > EDITOR_MAX_SPLIT_NODES) return;
    editor_store_current_buffer(e);
    editor_sync_active_view(e);
    size_t buffer_index = e->current_buffer;
    bool opened_path = false;
    if (path && path[0]) {
        if (!editor_open_buffer_index(e, path, &buffer_index)) {
            editor_set_status(e, "Split edit failed");
            return;
        }
        opened_path = true;
    }
    size_t new_view = tab->view_count++;
    tab->views[new_view] = make_view_from_editor(e, buffer_index);
    if (opened_path) {
        tab->views[new_view].cursor = 0;
        tab->views[new_view].visual_anchor = 0;
        tab->views[new_view].desired_col = -1;
        tab->views[new_view].top_line = 0;
        tab->views[new_view].left_col = 0;
    }
    tab->views[new_view].cursor_anim.initialized = false;
    int leaf = split_find_leaf_for_view(tab, tab->root, tab->active_view);
    if (leaf < 0) return;
    int old_leaf = (int)tab->node_count++;
    int new_leaf = (int)tab->node_count++;
    size_t old_view = tab->nodes[leaf].view_index;
    tab->nodes[old_leaf] = (SplitNode){.kind = SPLIT_LEAF, .ratio = 0.5f, .first = -1, .second = -1, .view_index = old_view};
    tab->nodes[new_leaf] = (SplitNode){.kind = SPLIT_LEAF, .ratio = 0.5f, .first = -1, .second = -1, .view_index = new_view};
    tab->nodes[leaf] = (SplitNode){.kind = vertical ? SPLIT_COL : SPLIT_ROW, .ratio = 0.5f, .first = old_leaf, .second = new_leaf, .view_index = 0};
    editor_load_view(e, new_view);
}

void editor_close_view(Editor *e, bool only) {
    EditorTab *tab = editor_active_tab(e);
    if (!tab) return;
    editor_store_current_buffer(e);
    editor_sync_active_view(e);
    if (only) {
        EditorView active = tab->views[tab->active_view];
        tab_init_single_view(tab, active.buffer_index, e);
        tab->views[0] = active;
        editor_load_view_state(e, 0, false);
        return;
    }
    if (tab->view_count <= 1) {
        editor_set_status(e, "Cannot close the last window");
        return;
    }
    size_t closing = tab->active_view;
    int leaf = split_find_leaf_for_view(tab, tab->root, closing);
    split_remove_leaf(tab, leaf);
    for (size_t i = closing + 1; i < tab->view_count; i++) tab->views[i - 1] = tab->views[i];
    tab->view_count--;
    for (size_t i = 0; i < tab->node_count; i++) {
        if (tab->nodes[i].kind == SPLIT_LEAF && tab->nodes[i].view_index > closing) tab->nodes[i].view_index--;
    }
    size_t next = closing >= tab->view_count ? tab->view_count - 1 : closing;
    editor_load_view_state(e, next, false);
}

static void collect_leaf_views(EditorTab *tab, int node_index, size_t *out, size_t *count) {
    if (node_index < 0 || (size_t)node_index >= tab->node_count) return;
    SplitNode *n = &tab->nodes[node_index];
    if (n->kind == SPLIT_LEAF) {
        out[(*count)++] = n->view_index;
        return;
    }
    collect_leaf_views(tab, n->first, out, count);
    collect_leaf_views(tab, n->second, out, count);
}

void editor_focus_view_direction(Editor *e, char dir) {
    EditorTab *tab = editor_active_tab(e);
    if (!tab || tab->view_count <= 1) return;
    editor_store_current_buffer(e);
    editor_sync_active_view(e);
    size_t leaves[EDITOR_MAX_VIEWS];
    size_t count = 0;
    collect_leaf_views(tab, tab->root, leaves, &count);
    if (count == 0) return;
    size_t at = 0;
    for (size_t i = 0; i < count; i++) {
        if (leaves[i] == tab->active_view) {
            at = i;
            break;
        }
    }
    size_t next = at;
    if (dir == 'h' || dir == 'k') next = at == 0 ? count - 1 : at - 1;
    else next = (at + 1) % count;
    editor_load_view(e, leaves[next]);
}

void editor_tab_new(Editor *e, const char *path) {
    if (e->tab_count >= EDITOR_MAX_TABS) {
        editor_set_status(e, "Too many tabs");
        return;
    }
    editor_store_current_buffer(e);
    editor_sync_active_view(e);
    size_t buffer_index = e->current_buffer;
    if (path && path[0]) {
        if (!editor_open_buffer_index(e, path, &buffer_index)) {
            editor_set_status(e, "Tab edit failed");
            return;
        }
    } else {
        editor_add_buffer(e, NULL, "", 0);
        buffer_index = e->buffer_count - 1;
    }
    size_t tab_index = e->tab_count++;
    e->current_tab = tab_index;
    tab_init_single_view(&e->tabs[tab_index], buffer_index, e);
    e->tabs[tab_index].views[0].cursor = 0;
    e->tabs[tab_index].views[0].visual_anchor = 0;
    e->tabs[tab_index].views[0].desired_col = -1;
    e->tabs[tab_index].views[0].top_line = 0;
    e->tabs[tab_index].views[0].left_col = 0;
    e->tabs[tab_index].views[0].cursor_anim.initialized = false;
    editor_load_view_state(e, 0, false);
}

void editor_tab_switch(Editor *e, int delta) {
    if (e->tab_count == 0) return;
    editor_store_current_buffer(e);
    editor_sync_active_view(e);
    if (delta > 0) e->current_tab = (e->current_tab + 1) % e->tab_count;
    else e->current_tab = e->current_tab == 0 ? e->tab_count - 1 : e->current_tab - 1;
    EditorTab *tab = editor_active_tab(e);
    editor_load_view_state(e, tab ? tab->active_view : 0, false);
}

void editor_tab_close(Editor *e) {
    if (e->tab_count <= 1) {
        editor_set_status(e, "Cannot close the last tab");
        return;
    }
    editor_store_current_buffer(e);
    size_t old = e->current_tab;
    memmove(&e->tabs[old], &e->tabs[old + 1], (e->tab_count - old - 1) * sizeof(*e->tabs));
    e->tab_count--;
    if (old >= e->tab_count) old = e->tab_count - 1;
    e->current_tab = old;
    EditorTab *tab = editor_active_tab(e);
    editor_load_view_state(e, tab ? tab->active_view : 0, false);
}

void editor_ensure_visible(Editor *e, int rows, int cols) {
    int line = byte_line(&e->text, e->cursor);
    int col = byte_col(&e->text, e->cursor);
    if (line < e->top_line) e->top_line = line;
    if (line >= e->top_line + rows) e->top_line = line - rows + 1;
    if (e->top_line < 0) e->top_line = 0;
    if (col < e->left_col) e->left_col = col;
    if (col >= e->left_col + cols) e->left_col = col - cols + 1;
    if (e->left_col < 0) e->left_col = 0;
}

static void editor_push_cursor_trail(Editor *e, int line, int col, Mode mode, double now) {
    if (line < 0 || col < 0) return;
    if (e->cursor_trail_len == CURSOR_TRAIL_MAX) {
        memmove(e->cursor_trail, e->cursor_trail + 1, sizeof(e->cursor_trail[0]) * (CURSOR_TRAIL_MAX - 1));
        e->cursor_trail_len--;
    }
    e->cursor_trail[e->cursor_trail_len++] = (CursorTrail){line, col, mode, now};
}

void editor_record_cursor_if_moved(Editor *e, int old_line, int old_col, Mode old_mode, double now) {
    int new_line = byte_line(&e->text, e->cursor);
    int new_col = byte_col(&e->text, e->cursor);
    if (old_line != new_line || old_col != new_col) {
        e->lsp.hover_visible = false;
        e->lsp.completion_visible = false;
        editor_push_cursor_trail(e, old_line, old_col, old_mode, now);
    }
}

void editor_push_jump(Editor *e) {
    if (e->jump_count > 0) {
        JumpLocation *last = &e->jumps[e->jump_count - 1];
        if (last->cursor == e->cursor && strcmp(last->path, e->path) == 0) return;
    }
    if (e->jump_count == EDITOR_JUMP_STACK_MAX) {
        memmove(e->jumps, e->jumps + 1, sizeof(e->jumps[0]) * (EDITOR_JUMP_STACK_MAX - 1));
        e->jump_count--;
    }
    JumpLocation *j = &e->jumps[e->jump_count++];
    snprintf(j->path, sizeof(j->path), "%s", e->path);
    j->cursor = e->cursor;
}

void editor_jump_back(Editor *e) {
    if (e->jump_count == 0) {
        snprintf(e->status, sizeof(e->status), "Jump list is empty");
        return;
    }
    JumpLocation j = e->jumps[--e->jump_count];
    if (j.path[0] && strcmp(j.path, e->path) != 0) {
        editor_open_buffer(e, j.path);
    }
    e->cursor = j.cursor <= e->text.len ? j.cursor : e->text.len;
    e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
    e->desired_col = byte_col(&e->text, e->cursor);
    e->lsp.hover_visible = false;
    e->lsp.completion_visible = false;
    e->lsp.completion_pending = false;
}

void editor_yank_range(Editor *e, size_t start, size_t end);

void editor_insert_char(Editor *e, unsigned int cp) {
    if (cp < 32 || cp > 126) return;
    ReplayEvent ev = {.kind = REPLAY_CHAR, .cp = cp};
    editor_macro_record_event(e, ev);
    if (!e->building_change && !e->replaying) editor_begin_recorded_change(e);
    editor_record_change_event(e, ev);
    editor_begin_insert_change(e);
    char c = (char)cp;
    text_insert(&e->text, e->cursor, &c, 1);
    editor_mark_text_changed(e);
    editor_record_insert_text(e, &c, 1);
    e->cursor++;
    e->desired_col = byte_col(&e->text, e->cursor);
    editor_reparse(e);
    if (!e->suppress_completion_request && (isalnum((unsigned char)c) || c == '_' || c == '.' || c == '>')) {
        e->lsp.completion_visible = false;
        e->lsp.completion_count = 0;
        e->lsp.completion_selected = 0;
        e->lsp.completion_scroll = 0;
        memset(e->lsp.completion_insert_texts, 0, sizeof(e->lsp.completion_insert_texts));
        e->lsp.completion_pending = true;
        e->lsp.completion_trigger_kind = (c == '.' || c == '>') ? 2 : 1;
        e->lsp.completion_trigger[0] = (c == '.' || c == '>') ? c : 0;
        e->lsp.completion_trigger[1] = 0;
    }
}

static void editor_backspace(Editor *e) {
    if (e->cursor == 0) return;
    editor_begin_insert_change(e);
    text_delete(&e->text, e->cursor - 1, 1);
    editor_mark_text_changed(e);
    e->cursor--;
    e->desired_col = byte_col(&e->text, e->cursor);
    editor_reparse(e);
}

static void editor_delete_char(Editor *e) {
    if (e->cursor >= e->text.len) return;
    editor_begin_change(e);
    text_delete(&e->text, e->cursor, 1);
    e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
    editor_reparse(e);
}

static void editor_delete_chars(Editor *e, int count) {
    if (count <= 0 || e->cursor >= e->text.len) return;
    size_t end = e->cursor;
    for (int i = 0; i < count && end < e->text.len && e->text.data[end] != '\n'; i++) end++;
    if (end <= e->cursor) return;
    editor_yank_range(e, e->cursor, end);
    editor_begin_change(e);
    text_delete(&e->text, e->cursor, end - e->cursor);
    e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
    editor_reparse(e);
}

static void editor_delete_left_chars(Editor *e, int count) {
    if (count <= 0 || e->cursor == 0) return;
    size_t start = e->cursor;
    for (int i = 0; i < count && start > 0 && e->text.data[start - 1] != '\n'; i++) start--;
    if (start >= e->cursor) return;
    editor_yank_range(e, start, e->cursor);
    editor_begin_change(e);
    text_delete(&e->text, start, e->cursor - start);
    e->cursor = clamp_cursor_for_normal(&e->text, start);
    e->desired_col = byte_col(&e->text, e->cursor);
    editor_reparse(e);
}

static void editor_newline(Editor *e) {
    if (e->mode == MODE_INSERT) editor_begin_insert_change(e);
    else editor_begin_change(e);
    text_insert(&e->text, e->cursor, "\n", 1);
    editor_mark_text_changed(e);
    if (e->mode == MODE_INSERT) editor_record_insert_text(e, "\n", 1);
    e->cursor++;
    e->desired_col = 0;
    editor_reparse(e);
}

static size_t line_indent_len(const Text *t, size_t pos) {
    size_t p = line_start_at(t, pos);
    size_t end = line_end_at(t, p);
    size_t start = p;
    while (p < end && (t->data[p] == ' ' || t->data[p] == '\t')) p++;
    return p - start;
}

static void editor_insert_bytes_for_insert(Editor *e, const char *s, size_t n) {
    if (n == 0) return;
    editor_begin_insert_change(e);
    text_insert(&e->text, e->cursor, s, n);
    editor_mark_text_changed(e);
    editor_record_insert_text(e, s, n);
    e->cursor += n;
    e->desired_col = byte_col(&e->text, e->cursor);
    editor_reparse(e);
}

static void editor_newline_autoindent(Editor *e, size_t indent_source_pos) {
    size_t indent_start = line_start_at(&e->text, indent_source_pos);
    size_t indent_len = line_indent_len(&e->text, indent_source_pos);
    char *indent = NULL;
    if (indent_len > 0) {
        indent = xmalloc(indent_len);
        memcpy(indent, e->text.data + indent_start, indent_len);
    }
    editor_newline(e);
    if (indent_len > 0) {
        editor_insert_bytes_for_insert(e, indent, indent_len);
        free(indent);
    }
}

static void editor_insert_tab(Editor *e) {
    int col = byte_col(&e->text, e->cursor);
    int spaces = 4 - (col % 4);
    if (spaces <= 0) spaces = 4;
    char buf[4] = {' ', ' ', ' ', ' '};
    editor_insert_bytes_for_insert(e, buf, (size_t)spaces);
}

static void editor_delete_line(Editor *e) {
    editor_begin_change(e);
    size_t start = line_start_at(&e->text, e->cursor);
    size_t end = line_end_at(&e->text, e->cursor);
    if (end < e->text.len) end++;
    if (start == end && e->text.len) end = e->text.len;
    text_delete(&e->text, start, end - start);
    if (start > e->text.len) start = e->text.len;
    e->cursor = clamp_cursor_for_normal(&e->text, start);
    e->desired_col = 0;
    editor_reparse(e);
}

void editor_yank_range(Editor *e, size_t start, size_t end) {
    if (end < start) return;
    text_set(&e->yank, e->text.data + start, end - start);
    e->has_yank = true;
    e->yank_linewise = false;
    e->yank_blockwise = false;
    if (e->pending_register >= 'a' && e->pending_register <= 'z') {
        int r = e->pending_register - 'a';
        text_set(&e->registers[r], e->yank.data, e->yank.len);
        e->has_register[r] = true;
        e->register_linewise[r] = e->yank_linewise;
        e->register_blockwise[r] = e->yank_blockwise;
    }
}

void editor_sync_pending_register_flags(Editor *e) {
    if (e->pending_register >= 'a' && e->pending_register <= 'z') {
        int r = e->pending_register - 'a';
        e->register_linewise[r] = e->yank_linewise;
        e->register_blockwise[r] = e->yank_blockwise;
    }
}

static void editor_yank_line(Editor *e) {
    size_t start = line_start_at(&e->text, e->cursor);
    size_t end = line_end_at(&e->text, e->cursor);
    if (end < e->text.len) end++;
    editor_yank_range(e, start, end);
    e->yank_linewise = true;
    editor_sync_pending_register_flags(e);
}

static size_t line_col_to_pos(const Text *t, int line, int col) {
    size_t start = line_start_by_number(t, line);
    size_t end = line_end_at(t, start);
    size_t pos = start + (size_t)(col < 0 ? 0 : col);
    return pos > end ? end : pos;
}

static void visual_block_bounds(const Editor *e, int *line0, int *line1, int *col0, int *col1) {
    int a_line = byte_line(&e->text, e->visual_anchor);
    int b_line = byte_line(&e->text, e->cursor);
    int a_col = byte_col(&e->text, e->visual_anchor);
    int b_col = byte_col(&e->text, e->cursor);
    if (a_line > b_line) {
        int t = a_line;
        a_line = b_line;
        b_line = t;
    }
    if (a_col > b_col) {
        int t = a_col;
        a_col = b_col;
        b_col = t;
    }
    *line0 = a_line;
    *line1 = b_line;
    *col0 = a_col;
    *col1 = b_col;
}

static void editor_yank_visual_block(Editor *e) {
    int line0, line1, col0, col1;
    visual_block_bounds(e, &line0, &line1, &col0, &col1);
    Text out;
    text_init(&out);
    out.len = 0;
    out.data[0] = 0;
    for (int line = line0; line <= line1; line++) {
        size_t start = line_col_to_pos(&e->text, line, col0);
        size_t end = line_col_to_pos(&e->text, line, col1 + 1);
        if (end > start) text_insert(&out, out.len, e->text.data + start, end - start);
        if (line != line1) text_insert(&out, out.len, "\n", 1);
    }
    text_set(&e->yank, out.data, out.len);
    text_free(&out);
    e->has_yank = true;
    e->yank_linewise = false;
    e->yank_blockwise = true;
    if (e->pending_register >= 'a' && e->pending_register <= 'z') {
        int r = e->pending_register - 'a';
        text_set(&e->registers[r], e->yank.data, e->yank.len);
        e->has_register[r] = true;
        e->register_linewise[r] = false;
        e->register_blockwise[r] = true;
    }
}

static void editor_remember_visual(Editor *e) {
    if (e->mode != MODE_VISUAL) return;
    e->last_visual_anchor = e->visual_anchor;
    e->last_visual_cursor = e->cursor;
    e->last_visual_line = e->visual_line;
    e->last_visual_block = e->visual_block;
}

static void editor_delete_visual_block(Editor *e) {
    int line0, line1, col0, col1;
    visual_block_bounds(e, &line0, &line1, &col0, &col1);
    editor_yank_visual_block(e);
    editor_remember_visual(e);
    editor_begin_change(e);
    for (int line = line1; line >= line0; line--) {
        size_t start = line_col_to_pos(&e->text, line, col0);
        size_t end = line_col_to_pos(&e->text, line, col1 + 1);
        if (end > start) text_delete(&e->text, start, end - start);
    }
    e->cursor = clamp_cursor_for_normal(&e->text, line_col_to_pos(&e->text, line0, col0));
    e->desired_col = byte_col(&e->text, e->cursor);
    e->mode = MODE_NORMAL;
    e->visual_block = false;
    editor_reparse(e);
}

static void editor_replace_visual_block(Editor *e, char ch) {
    int line0, line1, col0, col1;
    visual_block_bounds(e, &line0, &line1, &col0, &col1);
    editor_begin_change(e);
    for (int line = line0; line <= line1; line++) {
        size_t start = line_col_to_pos(&e->text, line, col0);
        size_t end = line_col_to_pos(&e->text, line, col1 + 1);
        for (size_t p = start; p < end; p++) {
            if (e->text.data[p] != '\n') e->text.data[p] = ch;
        }
    }
    e->cursor = clamp_cursor_for_normal(&e->text, line_col_to_pos(&e->text, line0, col0));
    e->desired_col = byte_col(&e->text, e->cursor);
    editor_remember_visual(e);
    e->mode = MODE_NORMAL;
    e->visual_block = false;
    e->visual_line = false;
    editor_reparse(e);
}

static void editor_start_block_insert(Editor *e, bool append) {
    int line0, line1, col0, col1;
    visual_block_bounds(e, &line0, &line1, &col0, &col1);
    e->block_insert_active = true;
    e->block_insert_append = append;
    e->block_insert_line0 = line0;
    e->block_insert_line1 = line1;
    e->block_insert_col = append ? col1 + 1 : col0;
    e->block_insert_len = 0;
    e->block_insert_text[0] = 0;
    e->cursor = line_col_to_pos(&e->text, byte_line(&e->text, e->cursor), e->block_insert_col);
    e->block_insert_primary_start = e->cursor;
    e->mode = MODE_INSERT;
    e->visual_block = false;
    e->visual_line = false;
    e->suppress_next_char = true;
}

static void editor_finish_block_insert(Editor *e) {
    if (!e->block_insert_active) return;
    size_t start = e->block_insert_primary_start;
    size_t end = e->cursor;
    if (end < start) end = start;
    if (end > e->text.len) end = e->text.len;
    size_t n = end - start;
    if (n >= sizeof(e->block_insert_text)) n = sizeof(e->block_insert_text) - 1;
    if (n > 0) {
        memcpy(e->block_insert_text, e->text.data + start, n);
        e->block_insert_text[n] = 0;
        e->block_insert_len = n;
        int primary = byte_line(&e->text, start);
        for (int line = e->block_insert_line1; line >= e->block_insert_line0; line--) {
            if (line == primary) continue;
            size_t pos = line_col_to_pos(&e->text, line, e->block_insert_col);
            text_insert(&e->text, pos, e->block_insert_text, e->block_insert_len);
        }
        editor_reparse(e);
    }
    e->block_insert_active = false;
}

static void editor_paste_block(Editor *e, bool before) {
    if (!e->has_yank || !e->yank_blockwise || e->yank.len == 0) return;
    int start_line = byte_line(&e->text, e->cursor);
    int start_col = byte_col(&e->text, e->cursor) + (before ? 0 : 1);
    editor_begin_change(e);
    size_t seg_start = 0;
    int line_delta = 0;
    while (seg_start <= e->yank.len) {
        size_t seg_end = seg_start;
        while (seg_end < e->yank.len && e->yank.data[seg_end] != '\n') seg_end++;
        int line = start_line + line_delta;
        while (line >= line_count(&e->text)) text_insert(&e->text, e->text.len, "\n", 1);
        size_t pos = line_col_to_pos(&e->text, line, start_col);
        text_insert(&e->text, pos, e->yank.data + seg_start, seg_end - seg_start);
        if (seg_end >= e->yank.len) break;
        seg_start = seg_end + 1;
        line_delta++;
    }
    e->cursor = clamp_cursor_for_normal(&e->text, line_col_to_pos(&e->text, start_line, start_col));
    editor_reparse(e);
}

static void editor_paste(Editor *e, bool before) {
    const Text *src = &e->yank;
    bool linewise = e->yank_linewise;
    bool blockwise = e->yank_blockwise;
    if (e->pending_register >= 'a' && e->pending_register <= 'z') {
        int r = e->pending_register - 'a';
        if (!e->has_register[r]) return;
        src = &e->registers[r];
        linewise = e->register_linewise[r];
        blockwise = e->register_blockwise[r];
    } else if (!e->has_yank) {
        return;
    }
    if (src->len == 0) return;
    if (blockwise) {
        if (src != &e->yank) {
            text_set(&e->yank, src->data, src->len);
            e->has_yank = true;
            e->yank_linewise = false;
            e->yank_blockwise = true;
        }
        editor_paste_block(e, before);
        return;
    }
    editor_begin_change(e);
    size_t pos = e->cursor;
    if (linewise) {
        if (before) {
            pos = line_start_at(&e->text, e->cursor);
        } else {
            pos = line_end_at(&e->text, e->cursor);
            if (pos < e->text.len) pos++;
        }
    } else {
        if (!before && pos < e->text.len) pos++;
    }
    text_insert(&e->text, pos, src->data, src->len);
    e->cursor = clamp_cursor_for_normal(&e->text, pos);
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
    if (e->visual_line) {
        a = line_start_at(&e->text, a);
        b = line_end_at(&e->text, b);
        if (b < e->text.len) b++;
    } else if (b < e->text.len) {
        b++;
    }
    editor_yank_range(e, a, b);
    e->yank_linewise = e->visual_line;
    editor_sync_pending_register_flags(e);
    editor_remember_visual(e);
    editor_begin_change(e);
    text_delete(&e->text, a, b - a);
    e->cursor = clamp_cursor_for_normal(&e->text, a);
    e->mode = MODE_NORMAL;
    editor_reparse(e);
}

void editor_delete_range(Editor *e, size_t a, size_t b, bool linewise) {
    if (a > b) {
        size_t t = a;
        a = b;
        b = t;
    }
    if (linewise) {
        a = line_start_at(&e->text, a);
        b = line_end_at(&e->text, b);
        if (b < e->text.len) b++;
    } else if (b < e->text.len) {
        b++;
    }
    if (a >= b) return;
    editor_yank_range(e, a, b);
    e->yank_linewise = linewise;
    editor_sync_pending_register_flags(e);
    editor_begin_change(e);
    text_delete(&e->text, a, b - a);
    e->cursor = clamp_cursor_for_normal(&e->text, a);
    e->desired_col = byte_col(&e->text, e->cursor);
    e->mode = MODE_NORMAL;
    editor_reparse(e);
}

static void editor_change_range(Editor *e, size_t a, size_t b, bool linewise) {
    editor_delete_range(e, a, b, linewise);
    e->mode = MODE_INSERT;
    editor_continue_insert_change(e);
    e->suppress_next_char = true;
}

static void editor_replace_char(Editor *e, char c) {
    if (e->cursor >= e->text.len || e->text.data[e->cursor] == '\n') return;
    editor_begin_change(e);
    e->text.data[e->cursor] = c;
    editor_reparse(e);
}

static void editor_toggle_case_range(Editor *e, size_t a, size_t b) {
    if (a > b) {
        size_t t = a;
        a = b;
        b = t;
    }
    if (b >= e->text.len) b = e->text.len ? e->text.len - 1 : 0;
    if (a > b || e->text.len == 0) return;
    editor_begin_change(e);
    for (size_t i = a; i <= b; i++) {
        unsigned char ch = (unsigned char)e->text.data[i];
        if (islower(ch)) e->text.data[i] = (char)toupper(ch);
        else if (isupper(ch)) e->text.data[i] = (char)tolower(ch);
    }
    e->cursor = clamp_cursor_for_normal(&e->text, b);
    e->desired_col = byte_col(&e->text, e->cursor);
    editor_reparse(e);
}

static void editor_toggle_case(Editor *e, int count) {
    if (e->mode == MODE_VISUAL) {
        size_t a = e->visual_anchor;
        size_t b = e->cursor;
        if (e->visual_line) {
            a = line_start_at(&e->text, a);
            b = line_end_at(&e->text, b);
        }
        editor_remember_visual(e);
        editor_toggle_case_range(e, a, b);
        e->mode = MODE_NORMAL;
        e->visual_block = false;
        e->visual_line = false;
        return;
    }
    if (count <= 0) count = 1;
    size_t end = e->cursor;
    for (int i = 1; i < count && end + 1 < e->text.len && e->text.data[end + 1] != '\n'; i++) end++;
    editor_toggle_case_range(e, e->cursor, end);
}

static void editor_join_lines(Editor *e, int count) {
    if (count <= 0) count = 1;
    size_t p = line_end_at(&e->text, e->cursor);
    if (p >= e->text.len) return;
    editor_begin_change(e);
    for (int i = 0; i < count && p < e->text.len; i++) {
        text_delete(&e->text, p, 1);
        while (p < e->text.len && (e->text.data[p] == ' ' || e->text.data[p] == '\t')) {
            text_delete(&e->text, p, 1);
        }
        if (p < e->text.len && p > 0 && e->text.data[p - 1] != ' ') {
            text_insert(&e->text, p, " ", 1);
            p++;
        }
        p = line_end_at(&e->text, p);
    }
    e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
    e->desired_col = byte_col(&e->text, e->cursor);
    editor_reparse(e);
}

static void editor_repeat_change(Editor *e) {
    if (e->last_change_event_len > 0) {
        e->macro_replay_steps = 0;
        editor_replay_events(e, e->last_change_events, e->last_change_event_len, 20);
    } else if (strcmp(e->last_change, "insert") == 0 && e->last_insert_len > 0) {
        editor_begin_change(e);
        text_insert(&e->text, e->cursor, e->last_insert, e->last_insert_len);
        e->cursor += e->last_insert_len;
        if (e->cursor > 0) e->cursor--;
        e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
        e->desired_col = byte_col(&e->text, e->cursor);
        editor_reparse(e);
    } else if (strcmp(e->last_change, "x") == 0) editor_delete_char(e);
    else if (strcmp(e->last_change, "p") == 0) editor_paste(e, false);
    else if (strcmp(e->last_change, "P") == 0) editor_paste(e, true);
    else if (strcmp(e->last_change, "dd") == 0) {
        editor_yank_line(e);
        editor_delete_line(e);
    } else if (strcmp(e->last_change, "D") == 0) {
        size_t end = line_end_at(&e->text, e->cursor);
        editor_begin_change(e);
        editor_yank_range(e, e->cursor, end);
        text_delete(&e->text, e->cursor, end - e->cursor);
        e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
        editor_reparse(e);
    } else if (strcmp(e->last_change, "~") == 0) {
        editor_toggle_case(e, 1);
    } else if (e->last_change[0] == 'r' && e->last_change[1]) {
        editor_replace_char(e, e->last_change[1]);
    }
}

static bool editor_motion(Editor *e, char cmd, int count, bool shift, int rows, size_t *out, bool *linewise) {
    if (count <= 0) count = 1;
    *linewise = false;
    size_t pos = e->cursor;
    switch (cmd) {
    case 'h':
        for (int i = 0; i < count; i++) {
            if (pos == 0 || e->text.data[pos - 1] == '\n') break;
            pos--;
        }
        break;
    case 'l':
        for (int i = 0; i < count; i++) {
            if (pos >= e->text.len || e->text.data[pos] == '\n') break;
            pos++;
        }
        pos = clamp_cursor_for_normal(&e->text, pos);
        break;
    case 'j':
    case 'k': {
        int line = byte_line(&e->text, e->cursor) + (cmd == 'j' ? count : -count);
        int col = e->desired_col >= 0 ? e->desired_col : byte_col(&e->text, e->cursor);
        int lc = line_count(&e->text);
        if (line < 0) line = 0;
        if (line >= lc) line = lc - 1;
        size_t start = line_start_by_number(&e->text, line);
        size_t end = line_end_at(&e->text, start);
        pos = start + (size_t)col;
        if (pos > end) pos = end;
        pos = clamp_cursor_for_normal(&e->text, pos);
        break;
    }
    case 'w': pos = word_forward_pos(&e->text, pos, count); break;
    case 'b': pos = word_back_pos(&e->text, pos, count); break;
    case 'e':
        pos = e->pending == 'g' ? word_prev_end_pos(&e->text, pos, count, false) : word_end_pos(&e->text, pos, count);
        break;
    case 'W': pos = big_word_forward_pos(&e->text, pos, count); break;
    case 'B': pos = big_word_back_pos(&e->text, pos, count); break;
    case 'E':
        pos = e->pending == 'g' ? word_prev_end_pos(&e->text, pos, count, true) : big_word_end_pos(&e->text, pos, count);
        break;
    case '0': pos = line_start_at(&e->text, pos); break;
    case '$': pos = line_end_at(&e->text, pos); pos = clamp_cursor_for_normal(&e->text, pos); break;
    case '^': pos = first_nonblank_on_line(&e->text, pos); break;
    case '_':
        pos = e->pending == 'g' ? line_last_nonblank(&e->text, pos) : first_nonblank_on_line(&e->text, pos);
        break;
    case '|': {
        size_t start = line_start_at(&e->text, pos);
        size_t end = line_end_at(&e->text, pos);
        size_t col = (size_t)(count > 0 ? count - 1 : 0);
        pos = start + col;
        if (pos > end) pos = end;
        pos = clamp_cursor_for_normal(&e->text, pos);
        break;
    }
    case '+': pos = next_line_first_nonblank(&e->text, pos, count); break;
    case '-': pos = next_line_first_nonblank(&e->text, pos, -count); break;
    case 'g':
        if (e->pending == 'g') {
            pos = count > 1 ? line_start_by_number(&e->text, count - 1) : 0;
            *linewise = true;
        }
        else return false;
        break;
    case '}': pos = paragraph_pos(&e->text, pos, count, 1); break;
    case '{': pos = paragraph_pos(&e->text, pos, count, -1); break;
    case 'G':
        pos = count > 1 ? line_start_by_number(&e->text, count - 1) : e->text.len;
        pos = clamp_cursor_for_normal(&e->text, pos);
        break;
    case 'H':
        pos = line_start_by_number(&e->text, e->top_line + count - 1);
        break;
    case 'M':
        pos = line_start_by_number(&e->text, e->top_line + rows / 2);
        break;
    case 'L':
        pos = line_start_by_number(&e->text, e->top_line + rows - count);
        break;
    case '%':
        if (!editor_find_matching_pair(e, &pos)) return false;
        break;
    default:
        (void)shift;
        return false;
    }
    *out = pos;
    return true;
}

static void editor_finish_operator(Editor *e, char motion, int motion_count, bool shift, int rows) {
    size_t pos = e->cursor;
    bool linewise = false;
    int count = (e->operator_count > 0 ? e->operator_count : 1) * (motion_count > 0 ? motion_count : 1);
    if (motion == e->operator_pending && (motion == 'd' || motion == 'c' || motion == 'y')) {
        linewise = true;
        pos = line_start_by_number(&e->text, byte_line(&e->text, e->cursor) + count - 1);
    } else if (!editor_motion(e, motion, count, shift, rows, &pos, &linewise)) {
        return;
    }
    if (motion == 'w' && !linewise) {
        if (e->operator_pending == 'c') {
            pos = word_end_pos(&e->text, e->cursor, count);
        } else if (pos > e->cursor) {
            pos--;
        }
    }
    if (e->operator_pending == 'd') {
        editor_delete_range(e, e->cursor, pos, linewise);
        snprintf(e->last_change, sizeof(e->last_change), "%c%c", 'd', motion);
    } else if (e->operator_pending == 'c') {
        editor_change_range(e, e->cursor, pos, linewise);
        snprintf(e->last_change, sizeof(e->last_change), "%c%c", 'c', motion);
    }
    else if (e->operator_pending == 'y') {
        size_t a = e->cursor, b = pos;
        if (a > b) {
            size_t t = a;
            a = b;
            b = t;
        }
        if (linewise) {
            a = line_start_at(&e->text, a);
            b = line_end_at(&e->text, b);
            if (b < e->text.len) b++;
        } else if (b < e->text.len) {
            b++;
        }
        editor_yank_range(e, a, b);
        e->yank_linewise = linewise;
        editor_sync_pending_register_flags(e);
        e->mode = MODE_NORMAL;
    }
    e->operator_pending = 0;
    e->operator_count = 0;
    e->count = 0;
    e->pending = 0;
    e->pending_register = 0;
    editor_commit_recorded_change(e);
}

static bool editor_word_object(Editor *e, bool around, size_t *a, size_t *b) {
    if (e->text.len == 0) return false;
    size_t p = e->cursor;
    if (p >= e->text.len) p = e->text.len - 1;
    while (p > 0 && !is_word_byte(e->text.data[p])) p--;
    if (!is_word_byte(e->text.data[p])) return false;
    size_t start = p;
    size_t end = p;
    while (start > 0 && is_word_byte(e->text.data[start - 1])) start--;
    while (end + 1 < e->text.len && is_word_byte(e->text.data[end + 1])) end++;
    if (around) {
        while (end + 1 < e->text.len && (e->text.data[end + 1] == ' ' || e->text.data[end + 1] == '\t')) end++;
        while (start > 0 && (e->text.data[start - 1] == ' ' || e->text.data[start - 1] == '\t')) start--;
    }
    *a = start;
    *b = end;
    return true;
}

static bool editor_big_word_object(Editor *e, bool around, size_t *a, size_t *b) {
    if (e->text.len == 0) return false;
    size_t p = e->cursor;
    if (p >= e->text.len) p = e->text.len - 1;
    while (p > 0 && !is_big_word_byte(e->text.data[p])) p--;
    if (!is_big_word_byte(e->text.data[p])) return false;
    size_t start = p;
    size_t end = p;
    while (start > 0 && is_big_word_byte(e->text.data[start - 1])) start--;
    while (end + 1 < e->text.len && is_big_word_byte(e->text.data[end + 1])) end++;
    if (around) {
        while (end + 1 < e->text.len && (e->text.data[end + 1] == ' ' || e->text.data[end + 1] == '\t')) end++;
        while (start > 0 && (e->text.data[start - 1] == ' ' || e->text.data[start - 1] == '\t')) start--;
    }
    *a = start;
    *b = end;
    return true;
}

static bool editor_paragraph_object(Editor *e, bool around, size_t *a, size_t *b) {
    if (e->text.len == 0) return false;
    int line = byte_line(&e->text, e->cursor);
    int lc = line_count(&e->text);
    if (line_is_blank(&e->text, line)) {
        while (line + 1 < lc && line_is_blank(&e->text, line)) line++;
        if (line >= lc || line_is_blank(&e->text, line)) return false;
    }
    int start_line = line;
    int end_line = line;
    while (start_line > 0 && !line_is_blank(&e->text, start_line - 1)) start_line--;
    while (end_line + 1 < lc && !line_is_blank(&e->text, end_line + 1)) end_line++;
    if (around) {
        while (end_line + 1 < lc && line_is_blank(&e->text, end_line + 1)) end_line++;
    }
    *a = line_start_by_number(&e->text, start_line);
    *b = line_end_at(&e->text, line_start_by_number(&e->text, end_line));
    if (*b < e->text.len) (*b)++;
    if (*b > 0) (*b)--;
    return *a <= *b;
}

static bool delimiter_pair(char object, char *open, char *close, bool *quote) {
    *quote = false;
    switch (object) {
    case '(':
    case ')': *open = '('; *close = ')'; return true;
    case '[':
    case ']': *open = '['; *close = ']'; return true;
    case '{':
    case '}': *open = '{'; *close = '}'; return true;
    case '<':
    case '>': *open = '<'; *close = '>'; return true;
    case '"':
    case '\'':
    case '`': *open = object; *close = object; *quote = true; return true;
    default: return false;
    }
}

static bool editor_pair_object(Editor *e, bool around, char object, size_t *a, size_t *b) {
    char open = 0, close = 0;
    bool quote = false;
    if (!delimiter_pair(object, &open, &close, &quote) || e->text.len == 0) return false;
    size_t cursor = e->cursor < e->text.len ? e->cursor : e->text.len - 1;
    size_t left = SIZE_MAX;
    size_t right = SIZE_MAX;
    if (quote) {
        size_t line_start = line_start_at(&e->text, cursor);
        size_t line_end = line_end_at(&e->text, cursor);
        for (size_t i = cursor + 1; i-- > line_start;) {
            if (e->text.data[i] == open) {
                left = i;
                break;
            }
            if (i == 0) break;
        }
        for (size_t i = cursor + (left == cursor ? 1 : 0); i < line_end; i++) {
            if (i != left && e->text.data[i] == close) {
                right = i;
                break;
            }
        }
    } else {
        int depth = 0;
        for (size_t i = cursor + 1; i-- > 0;) {
            char c = e->text.data[i];
            if (c == close) depth++;
            else if (c == open) {
                if (depth == 0) {
                    left = i;
                    break;
                }
                depth--;
            }
            if (i == 0) break;
        }
        depth = 0;
        size_t start = left != SIZE_MAX ? left : cursor;
        for (size_t i = start; i < e->text.len; i++) {
            char c = e->text.data[i];
            if (c == open) depth++;
            else if (c == close) {
                depth--;
                if (depth == 0) {
                    right = i;
                    break;
                }
            }
        }
    }
    if (left == SIZE_MAX || right == SIZE_MAX || left >= right) return false;
    if (around) {
        *a = left;
        *b = right;
    } else {
        *a = left + 1;
        *b = right > 0 ? right - 1 : right;
    }
    return *a <= *b || !around;
}

static void editor_finish_text_object(Editor *e, bool around, char object) {
    if (!e->operator_pending) return;
    size_t a = 0, b = 0;
    if (object == 'w') {
        if (!editor_word_object(e, around, &a, &b)) return;
    } else if (object == 'W') {
        if (!editor_big_word_object(e, around, &a, &b)) return;
    } else if (object == 'p') {
        if (!editor_paragraph_object(e, around, &a, &b)) return;
    } else {
        if (!editor_pair_object(e, around, object, &a, &b)) return;
    }
    if (e->operator_pending == 'd') {
        if (a <= b) editor_delete_range(e, a, b, false);
    } else if (e->operator_pending == 'c') {
        if (a <= b) editor_change_range(e, a, b, false);
        else {
            e->cursor = clamp_cursor_for_normal(&e->text, a);
            e->mode = MODE_INSERT;
            e->suppress_next_char = true;
        }
    } else if (e->operator_pending == 'y') {
        if (a <= b) editor_yank_range(e, a, b + 1);
        editor_sync_pending_register_flags(e);
        e->mode = MODE_NORMAL;
    }
    e->operator_pending = 0;
    e->operator_count = 0;
    e->count = 0;
    e->pending_register = 0;
}

static char key_to_motion_char(int key, bool shift) {
    if (key == GLFW_KEY_LEFT) return 'h';
    if (key == GLFW_KEY_DOWN) return 'j';
    if (key == GLFW_KEY_UP) return 'k';
    if (key == GLFW_KEY_RIGHT) return 'l';
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        char c = (char)('a' + key - GLFW_KEY_A);
        return shift ? (char)toupper((unsigned char)c) : c;
    }
    if (key == GLFW_KEY_2 && shift) return '@';
    if (key == GLFW_KEY_4 && shift) return '$';
    if (key == GLFW_KEY_6 && shift) return '^';
    if (key == GLFW_KEY_5 && shift) return '%';
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) return (char)('1' + key - GLFW_KEY_1);
    if (key == GLFW_KEY_0) return '0';
    if (key == GLFW_KEY_MINUS) return shift ? '_' : '-';
    if (key == GLFW_KEY_EQUAL && shift) return '+';
    if (key == GLFW_KEY_SEMICOLON) return shift ? ':' : ';';
    if (key == GLFW_KEY_COMMA) return ',';
    if (key == GLFW_KEY_SLASH) return shift ? '?' : '/';
    if (key == GLFW_KEY_PERIOD) return '.';
    if (key == GLFW_KEY_LEFT_BRACKET && shift) return '{';
    if (key == GLFW_KEY_RIGHT_BRACKET && shift) return '}';
    if (key == GLFW_KEY_BACKSLASH && shift) return '|';
    if (key == GLFW_KEY_APOSTROPHE) return shift ? '"' : '\'';
    if (key == GLFW_KEY_GRAVE_ACCENT) return shift ? '~' : '`';
    return 0;
}

typedef enum {
    EDITOR_KEYMAP_ESCAPE,
    EDITOR_KEYMAP_CTRL_O,
    EDITOR_KEYMAP_INSERT_BACKSPACE,
    EDITOR_KEYMAP_INSERT_ENTER,
    EDITOR_KEYMAP_INSERT_TAB,
    EDITOR_KEYMAP_INSERT_LEFT,
    EDITOR_KEYMAP_INSERT_RIGHT,
    EDITOR_KEYMAP_INSERT_UP,
    EDITOR_KEYMAP_INSERT_DOWN,
} EditorKeymapAction;

#define KM_KEY(key_value) .key = (key_value)
#define KM_MODE(mode_value) .mode_exact = true, .mode = (mode_value)

static const KeymapBinding editor_keymap_bindings[] = {
    {KM_KEY(GLFW_KEY_ESCAPE), .priority = 100},
    {KM_KEY(GLFW_KEY_O), .mods_required = GLFW_MOD_CONTROL, .priority = 90},
    {KM_KEY(GLFW_KEY_BACKSPACE), KM_MODE(MODE_INSERT), .priority = 80},
    {KM_KEY(GLFW_KEY_ENTER), KM_MODE(MODE_INSERT), .priority = 80},
    {KM_KEY(GLFW_KEY_TAB), KM_MODE(MODE_INSERT), .priority = 80},
    {KM_KEY(GLFW_KEY_LEFT), KM_MODE(MODE_INSERT), .priority = 80},
    {KM_KEY(GLFW_KEY_RIGHT), KM_MODE(MODE_INSERT), .priority = 80},
    {KM_KEY(GLFW_KEY_UP), KM_MODE(MODE_INSERT), .priority = 80},
    {KM_KEY(GLFW_KEY_DOWN), KM_MODE(MODE_INSERT), .priority = 80},
};

static const EditorKeymapAction editor_keymap_actions[] = {
    EDITOR_KEYMAP_ESCAPE,
    EDITOR_KEYMAP_CTRL_O,
    EDITOR_KEYMAP_INSERT_BACKSPACE,
    EDITOR_KEYMAP_INSERT_ENTER,
    EDITOR_KEYMAP_INSERT_TAB,
    EDITOR_KEYMAP_INSERT_LEFT,
    EDITOR_KEYMAP_INSERT_RIGHT,
    EDITOR_KEYMAP_INSERT_UP,
    EDITOR_KEYMAP_INSERT_DOWN,
};

static bool editor_apply_keymap_action(Editor *e, EditorKeymapAction action, int key, int mods, int rows, ReplayEvent ev) {
    (void)mods;
    (void)rows;
    switch (action) {
    case EDITOR_KEYMAP_ESCAPE:
        e->lsp.hover_visible = false;
        e->lsp.completion_visible = false;
        e->lsp.completion_pending = false;
        editor_macro_record_event(e, ev);
        editor_record_change_event(e, ev);
        if (e->mode == MODE_INSERT && e->block_insert_active) editor_finish_block_insert(e);
        if (e->mode == MODE_INSERT) editor_finish_insert_change(e);
        if (e->mode == MODE_VISUAL) editor_remember_visual(e);
        if (e->mode == MODE_INSERT && e->cursor > 0) e->cursor--;
        e->mode = MODE_NORMAL;
        e->pending = 0;
        e->operator_pending = 0;
        e->count = 0;
        e->operator_count = 0;
        e->waiting_char = 0;
        e->visual_block = false;
        e->visual_line = false;
        e->window_pending = 0;
        e->command_len = 0;
        e->command[0] = 0;
        e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
        e->desired_col = byte_col(&e->text, e->cursor);
        editor_commit_recorded_change(e);
        return true;
    case EDITOR_KEYMAP_CTRL_O:
        e->lsp.hover_visible = false;
        e->lsp.completion_visible = false;
        e->lsp.completion_pending = false;
        editor_jump_back(e);
        return true;
    case EDITOR_KEYMAP_INSERT_BACKSPACE:
    case EDITOR_KEYMAP_INSERT_ENTER:
    case EDITOR_KEYMAP_INSERT_TAB:
    case EDITOR_KEYMAP_INSERT_LEFT:
    case EDITOR_KEYMAP_INSERT_RIGHT:
    case EDITOR_KEYMAP_INSERT_UP:
    case EDITOR_KEYMAP_INSERT_DOWN:
        editor_macro_record_event(e, ev);
        if (!e->building_change && editor_event_changes_text(ev) && !e->replaying) editor_begin_recorded_change(e);
        editor_record_change_event(e, ev);
        if (key == GLFW_KEY_BACKSPACE) editor_backspace(e);
        else if (key == GLFW_KEY_ENTER) editor_newline_autoindent(e, e->cursor);
        else if (key == GLFW_KEY_TAB) editor_insert_tab(e);
        else if (key == GLFW_KEY_LEFT) editor_move_left(e);
        else if (key == GLFW_KEY_RIGHT) editor_move_right(e);
        else if (key == GLFW_KEY_UP) editor_move_vertical(e, -1);
        else if (key == GLFW_KEY_DOWN) editor_move_vertical(e, 1);
        return true;
    }
    return false;
}

static bool editor_dispatch_keymap(Editor *e, int key, int mods, int rows, char c, ReplayEvent ev) {
    KeymapProbe probe = {
        .key = key,
        .mods = mods,
        .ch = c,
        .ctx = {
            .mode = e->mode,
            .waiting_char = e->waiting_char,
            .operator_pending = e->operator_pending,
            .window_pending = e->window_pending,
        },
    };
    size_t index = keymap_resolve(editor_keymap_bindings, sizeof(editor_keymap_bindings) / sizeof(editor_keymap_bindings[0]), &probe, NULL);
    if (index == SIZE_MAX) return false;
    return editor_apply_keymap_action(e, editor_keymap_actions[index], key, mods, rows, ev);
}

#undef KM_KEY
#undef KM_MODE

void editor_key(Editor *e, int key, int mods, int rows) {
    bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    ReplayEvent ev = {.kind = REPLAY_KEY, .key = key, .mods = mods};
    if (key != GLFW_KEY_LEFT_SHIFT && key != GLFW_KEY_RIGHT_SHIFT) e->status[0] = 0;
    char c = key_to_motion_char(key, shift);
    if (editor_dispatch_keymap(e, key, mods, rows, c, ev)) return;
    if (e->mode == MODE_INSERT) return;
    if (!c) return;
    if (e->window_pending) {
        editor_macro_record_event(e, ev);
        if (c == 's') editor_split_current(e, false, NULL);
        else if (c == 'v') editor_split_current(e, true, NULL);
        else if (c == 'c' || c == 'q') editor_close_view(e, false);
        else if (c == 'o') editor_close_view(e, true);
        else if (c == 'w') editor_focus_view_direction(e, 'l');
        else if (c == 'h' || c == 'j' || c == 'k' || c == 'l') editor_focus_view_direction(e, c);
        e->window_pending = 0;
        e->count = 0;
        return;
    }
    if (c == 'q' && !e->operator_pending && e->mode == MODE_NORMAL) {
        if (e->macro_recording) {
            e->macro_recording = false;
            e->last_macro_register = e->macro_record_register;
            e->macro_record_register = 0;
            editor_set_status(e, "Recorded");
        } else {
            e->waiting_char = 'q';
            e->suppress_next_char = true;
        }
        return;
    }
    if (c == '@' && !e->operator_pending && e->mode == MODE_NORMAL) {
        e->waiting_char = '@';
        e->suppress_next_char = true;
        return;
    }
    editor_macro_record_event(e, ev);
    bool starts_change =
        (e->mode == MODE_VISUAL && (c == 'd' || c == 'c' || c == 'x' || c == 'X' || c == '~' || c == 'r')) ||
        c == 'i' || c == 'I' || c == 'a' || c == 'A' || c == 'o' || c == 'O' ||
        c == 'x' || c == 'X' || c == 'C' || c == 'J' || c == 's' || c == 'S' ||
        c == 'D' || c == 'p' || c == 'P' || c == 'r' || c == '~' || c == 'd' || c == 'c';
    if (starts_change && !e->building_change && !e->replaying) editor_begin_recorded_change(e);
    editor_record_change_event(e, ev);
    if (c == '"') {
        e->waiting_char = '"';
        e->suppress_next_char = true;
        return;
    }
    if (!e->operator_pending && c >= '1' && c <= '9') {
        e->count = e->count * 10 + (c - '0');
        return;
    }
    if (!e->operator_pending && c == '0' && e->count > 0) {
        e->count *= 10;
        return;
    }
    int count = e->count > 0 ? e->count : 1;
    if (e->operator_pending) {
        if (c >= '1' && c <= '9') {
            e->count = e->count * 10 + (c - '0');
            return;
        }
        if (c == 'g' && e->pending != 'g') {
            e->pending = 'g';
            return;
        }
        if (c == 'i' || c == 'a') {
            e->waiting_char = c;
            e->suppress_next_char = true;
            return;
        }
        if (c == 'f' || c == 'F' || c == 't' || c == 'T') {
            e->waiting_char = c;
            e->operator_count = e->operator_count > 0 ? e->operator_count : 1;
            e->count = count;
            e->suppress_next_char = true;
            return;
        }
        editor_finish_operator(e, c, count, shift, rows);
        return;
    }
    if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_R) {
        editor_redo(e);
    } else if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_D) {
        e->top_line += rows / 2 > 0 ? rows / 2 : 1;
    } else if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_U) {
        e->top_line -= rows / 2 > 0 ? rows / 2 : 1;
        if (e->top_line < 0) e->top_line = 0;
    } else if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_F) {
        e->top_line += rows;
        editor_move_vertical(e, rows);
    } else if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_B) {
        e->top_line -= rows;
        if (e->top_line < 0) e->top_line = 0;
        editor_move_vertical(e, -rows);
    } else if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_V) {
        if (e->mode == MODE_VISUAL && e->visual_block) {
            editor_remember_visual(e);
            e->mode = MODE_NORMAL;
            e->visual_block = false;
        } else {
            e->mode = MODE_VISUAL;
            e->visual_line = false;
            e->visual_block = true;
            e->visual_anchor = e->cursor;
        }
        e->pending = 0;
    } else if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_W) {
        e->window_pending = 'w';
        e->pending = 0;
    } else if (c == 'u') {
        editor_undo(e);
    } else if (c == 'K') {
        lsp_request_hover(e);
        e->pending = 0;
    } else if (c == 'm') {
        e->waiting_char = 'm';
        e->suppress_next_char = true;
    } else if (c == '\'' || c == '`') {
        e->waiting_char = c;
        e->suppress_next_char = true;
    } else if ((c == 'I' || c == 'A') && e->mode == MODE_VISUAL && e->visual_block) {
        editor_start_block_insert(e, c == 'A');
        e->pending = 0;
    } else if (c == 'i' || c == 'I') {
        if (c == 'I') editor_go_to(e, first_nonblank_on_line(&e->text, e->cursor));
        e->mode = MODE_INSERT;
        e->pending = 0;
        e->suppress_next_char = true;
    } else if (c == 'a' || c == 'A') {
        if (c == 'A') editor_move_line_end(e);
        if (e->cursor < e->text.len && e->text.data[e->cursor] != '\n') e->cursor++;
        e->mode = MODE_INSERT;
        e->pending = 0;
        e->suppress_next_char = true;
    } else if (c == 'v') {
        if (e->pending == 'g') {
            e->mode = MODE_VISUAL;
            e->visual_anchor = e->last_visual_anchor <= e->text.len ? e->last_visual_anchor : e->cursor;
            e->cursor = e->last_visual_cursor <= e->text.len ? e->last_visual_cursor : e->cursor;
            e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
            e->visual_line = e->last_visual_line;
            e->visual_block = e->last_visual_block;
        } else if (e->mode == MODE_VISUAL && !e->visual_line && !e->visual_block) {
            editor_remember_visual(e);
            e->mode = MODE_NORMAL;
        }
        else {
            e->mode = MODE_VISUAL;
            e->visual_line = false;
            e->visual_block = false;
            e->visual_anchor = e->cursor;
        }
        e->pending = 0;
    } else if (c == 'V') {
        if (e->mode == MODE_VISUAL && e->visual_line) {
            editor_remember_visual(e);
            e->mode = MODE_NORMAL;
        }
        else {
            e->mode = MODE_VISUAL;
            e->visual_line = true;
            e->visual_block = false;
            e->visual_anchor = line_start_at(&e->text, e->cursor);
        }
        e->pending = 0;
    } else if (c == '.') {
        editor_cancel_recorded_change(e);
        editor_repeat_change(e);
    } else if (c == '~') {
        editor_toggle_case(e, count);
        snprintf(e->last_change, sizeof(e->last_change), "~");
        editor_commit_recorded_change(e);
        e->pending = 0;
    } else if (c == 'x') {
        if (e->mode == MODE_VISUAL && e->visual_block) editor_delete_visual_block(e);
        else if (e->mode == MODE_VISUAL) editor_delete_selection(e);
        else editor_delete_chars(e, count);
        snprintf(e->last_change, sizeof(e->last_change), "x");
        e->pending_register = 0;
        editor_commit_recorded_change(e);
        e->pending = 0;
    } else if (c == 'X') {
        if (e->mode == MODE_VISUAL && e->visual_block) editor_delete_visual_block(e);
        else if (e->mode == MODE_VISUAL) editor_delete_selection(e);
        else editor_delete_left_chars(e, count);
        snprintf(e->last_change, sizeof(e->last_change), "X");
        e->pending_register = 0;
        editor_commit_recorded_change(e);
        e->pending = 0;
    } else if (c == 'C') {
        size_t end = line_end_at(&e->text, e->cursor);
        if (end > e->cursor) editor_delete_range(e, e->cursor, end - 1, false);
        e->mode = MODE_INSERT;
        e->suppress_next_char = true;
        snprintf(e->last_change, sizeof(e->last_change), "C");
        e->pending = 0;
    } else if (c == 'J') {
        editor_join_lines(e, count);
        snprintf(e->last_change, sizeof(e->last_change), "J");
        editor_commit_recorded_change(e);
        e->pending = 0;
    } else if (c == 's' || c == 'S') {
        if (c == 'S') {
            editor_yank_line(e);
            editor_delete_line(e);
        } else {
            size_t end = e->cursor + (size_t)count - 1;
            if (end >= e->text.len) end = e->text.len ? e->text.len - 1 : 0;
            editor_delete_range(e, e->cursor, end, false);
        }
        e->mode = MODE_INSERT;
        e->suppress_next_char = true;
        snprintf(e->last_change, sizeof(e->last_change), "%c", c);
    } else if ((c == 'd' || c == 'D') && e->pending == 'g') {
        lsp_request_definition(e, c == 'D');
        e->pending = 0;
        e->count = 0;
    } else if (c == 'D') {
        size_t end = line_end_at(&e->text, e->cursor);
        editor_begin_change(e);
        editor_yank_range(e, e->cursor, end);
        text_delete(&e->text, e->cursor, end - e->cursor);
        e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
        editor_reparse(e);
        snprintf(e->last_change, sizeof(e->last_change), "D");
        e->pending_register = 0;
        editor_commit_recorded_change(e);
    } else if (c == 'Y') {
        editor_yank_line(e);
        e->pending_register = 0;
        editor_cancel_recorded_change(e);
    } else if (c == 'd' || c == 'c' || c == 'y') {
        if (e->mode == MODE_VISUAL) {
            if (c == 'd') {
                if (e->visual_block) editor_delete_visual_block(e);
                else editor_delete_selection(e);
            }
            else if (c == 'c') {
                if (e->visual_block) editor_delete_visual_block(e);
                else editor_delete_selection(e);
                e->mode = MODE_INSERT;
                e->suppress_next_char = true;
            } else {
                if (e->visual_block) {
                    editor_yank_visual_block(e);
                } else {
                    size_t a = e->visual_anchor, b = e->cursor;
                    if (a > b) {
                        size_t t = a;
                        a = b;
                        b = t;
                    }
                    if (e->visual_line) {
                        a = line_start_at(&e->text, a);
                        b = line_end_at(&e->text, b);
                        if (b < e->text.len) b++;
                    } else if (b < e->text.len) b++;
                    editor_yank_range(e, a, b);
                    e->yank_linewise = e->visual_line;
                    editor_sync_pending_register_flags(e);
                }
                editor_remember_visual(e);
                e->mode = MODE_NORMAL;
                e->visual_block = false;
                e->pending_register = 0;
                editor_commit_recorded_change(e);
            }
        } else {
            e->operator_pending = c;
            e->operator_count = count;
            e->count = 0;
        }
    } else if (c == 'p' || c == 'P') {
        for (int i = 0; i < count; i++) editor_paste(e, c == 'P');
        snprintf(e->last_change, sizeof(e->last_change), "%c", c);
        e->pending_register = 0;
        editor_commit_recorded_change(e);
        e->pending = 0;
    } else if (c == 'r') {
        e->waiting_char = 'r';
        e->suppress_next_char = true;
    } else if ((c == 't' || c == 'T') && e->pending == 'g') {
        editor_tab_switch(e, c == 't' ? 1 : -1);
        e->pending = 0;
        e->count = 0;
    } else if (c == 'f' || c == 'F' || c == 't' || c == 'T') {
        e->waiting_char = c;
        e->operator_count = count;
        e->suppress_next_char = true;
    } else if (c == ';' || c == ',') {
        if (e->last_find_cmd && e->last_find_char) {
            char cmd = e->last_find_cmd;
            if (c == ',') {
                if (cmd == 'f') cmd = 'F';
                else if (cmd == 'F') cmd = 'f';
                else if (cmd == 't') cmd = 'T';
                else if (cmd == 'T') cmd = 't';
            }
            size_t pos = 0;
            if (editor_find_char_on_line(e, e->last_find_char, cmd, count, &pos)) editor_go_to(e, pos);
        }
    } else if (c == 'o' && e->mode == MODE_VISUAL) {
        size_t tmp = e->cursor;
        e->cursor = e->visual_anchor;
        e->visual_anchor = tmp;
        e->desired_col = byte_col(&e->text, e->cursor);
        e->pending = 0;
    } else if (c == 'o' || c == 'O') {
        if (c == 'O') {
            size_t start = line_start_at(&e->text, e->cursor);
            size_t indent_len = line_indent_len(&e->text, e->cursor);
            char *indent = NULL;
            if (indent_len > 0) {
                indent = xmalloc(indent_len);
                memcpy(indent, e->text.data + start, indent_len);
            }
            editor_begin_change(e);
            text_insert(&e->text, start, "\n", 1);
            e->cursor = start;
            if (indent_len > 0) {
                text_insert(&e->text, e->cursor, indent, indent_len);
                e->cursor += indent_len;
            }
            free(indent);
            e->desired_col = byte_col(&e->text, e->cursor);
            editor_reparse(e);
        } else {
            size_t end = line_end_at(&e->text, e->cursor);
            size_t source = e->cursor;
            e->cursor = end;
            editor_newline_autoindent(e, source);
        }
        e->mode = MODE_INSERT;
        e->pending = 0;
        e->suppress_next_char = true;
    } else if (c == ':') {
        e->mode = MODE_COMMAND;
        e->command_len = 0;
        e->command[0] = 0;
        e->pending = 0;
        e->suppress_next_char = true;
    } else if (c == '/' || c == '?') {
        e->mode = MODE_COMMAND;
        e->command_len = 1;
        e->command[0] = c;
        e->command[1] = 0;
        e->search_backward = c == '?';
        e->suppress_next_char = true;
    } else if (c == 'n') {
        editor_repeat_search(e, e->last_search_dir ? e->last_search_dir : 1);
    } else if (c == 'N') {
        editor_repeat_search(e, e->last_search_dir ? -e->last_search_dir : -1);
    } else if (c == 'z' && e->pending == 'z') {
        editor_scroll_cursor_to_fraction(e, rows, 1);
        e->pending = 0;
    } else if ((c == 't' || c == 'b') && e->pending == 'z') {
        editor_scroll_cursor_to_fraction(e, rows, c == 't' ? 0 : 2);
        e->pending = 0;
    } else if (c == 'z') {
        e->pending = 'z';
    } else {
        if (c == 'G' && e->count > 0) {
            editor_go_to(e, line_start_by_number(&e->text, e->count - 1));
            e->pending = 0;
            e->count = 0;
            return;
        }
        size_t pos = e->cursor;
        bool linewise = false;
        bool moved = editor_motion(e, c, count, shift, rows, &pos, &linewise);
        if (moved) editor_go_to(e, pos);
        e->pending = (c == 'g' && !moved) ? 'g' : 0;
        e->count = 0;
        (void)linewise;
    }
}

void editor_handle_waiting_char(Editor *e, char ch) {
    ReplayEvent ev = {.kind = REPLAY_WAITING_CHAR, .cp = (unsigned int)(unsigned char)ch};
    editor_macro_record_event(e, ev);
    editor_record_change_event(e, ev);
    if (e->waiting_char == 'r') {
        if (e->mode == MODE_VISUAL && e->visual_block) editor_replace_visual_block(e, ch);
        else editor_replace_char(e, ch);
        snprintf(e->last_change, sizeof(e->last_change), "r%c", ch);
        editor_commit_recorded_change(e);
    } else if (e->waiting_char == 'q') {
        if (ch >= 'A' && ch <= 'Z') ch = (char)tolower((unsigned char)ch);
        if (ch >= 'a' && ch <= 'z') {
            e->macro_recording = true;
            e->macro_record_register = ch;
            e->macros[ch - 'a'].len = 0;
            editor_set_status(e, "Recording");
        }
    } else if (e->waiting_char == '@') {
        if (ch == '@') ch = e->last_macro_register;
        if (ch >= 'A' && ch <= 'Z') ch = (char)tolower((unsigned char)ch);
        if (ch >= 'a' && ch <= 'z' && e->macros[ch - 'a'].len > 0) {
            e->last_macro_register = ch;
            e->macro_replay_steps = 0;
            editor_replay_events(e, e->macros[ch - 'a'].events, e->macros[ch - 'a'].len, 20);
        }
    } else if (e->waiting_char == '"') {
        if (ch >= 'A' && ch <= 'Z') ch = (char)tolower((unsigned char)ch);
        if (ch >= 'a' && ch <= 'z') e->pending_register = ch;
    } else if (e->waiting_char == 'm') {
        if (ch >= 'A' && ch <= 'Z') ch = (char)tolower((unsigned char)ch);
        if (ch >= 'a' && ch <= 'z') {
            int m = ch - 'a';
            e->marks[m] = e->cursor;
            e->marks_set[m] = true;
        }
    } else if (e->waiting_char == '\'' || e->waiting_char == '`') {
        if (ch >= 'A' && ch <= 'Z') ch = (char)tolower((unsigned char)ch);
        if (ch >= 'a' && ch <= 'z' && e->marks_set[ch - 'a']) {
            size_t pos = e->marks[ch - 'a'];
            if (pos > e->text.len) pos = e->text.len;
            if (e->waiting_char == '\'') pos = first_nonblank_on_line(&e->text, pos);
            editor_go_to(e, pos);
        } else {
            editor_set_status(e, "Mark not set");
        }
    } else if (e->waiting_char == 'f' || e->waiting_char == 'F' || e->waiting_char == 't' || e->waiting_char == 'T') {
        size_t pos = 0;
        int count = e->operator_count > 0 ? e->operator_count : 1;
        if (editor_find_char_on_line(e, ch, e->waiting_char, count, &pos)) {
            if (e->operator_pending) {
                if (e->operator_pending == 'd') editor_delete_range(e, e->cursor, pos, false);
                else if (e->operator_pending == 'c') editor_change_range(e, e->cursor, pos, false);
                else if (e->operator_pending == 'y') {
                    size_t a = e->cursor, b = pos;
                    if (a > b) {
                        size_t t = a;
                        a = b;
                        b = t;
                    }
                    if (b < e->text.len) b++;
                    editor_yank_range(e, a, b);
                }
                e->operator_pending = 0;
            } else {
                editor_go_to(e, pos);
                e->last_find_char = ch;
                e->last_find_cmd = e->waiting_char;
            }
        }
    } else if (e->waiting_char == 'i' || e->waiting_char == 'a') {
        editor_finish_text_object(e, e->waiting_char == 'a', ch);
    }
    e->waiting_char = 0;
    e->operator_count = 0;
    e->count = 0;
}

static bool editor_execute_event(Editor *e, ReplayEvent ev, int rows) {
    switch (ev.kind) {
    case REPLAY_KEY:
        editor_key(e, ev.key, ev.mods, rows);
        return true;
    case REPLAY_CHAR:
        editor_insert_char(e, ev.cp);
        return true;
    case REPLAY_WAITING_CHAR:
        editor_handle_waiting_char(e, (char)ev.cp);
        return true;
    }
    return false;
}
