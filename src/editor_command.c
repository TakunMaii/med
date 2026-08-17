#include "med.h"

static void editor_set_status(Editor *e, const char *msg) {
    snprintf(e->status, sizeof(e->status), "%s", msg ? msg : "");
}

static char *trim_command(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

static bool parse_ex_address(Editor *e, char **p, int *line) {
    if (**p == '.') {
        *line = byte_line(&e->text, e->cursor);
        (*p)++;
        return true;
    }
    if (**p == '$') {
        *line = line_count(&e->text) - 1;
        (*p)++;
        return true;
    }
    if (isdigit((unsigned char)**p)) {
        int n = 0;
        while (isdigit((unsigned char)**p)) {
            n = n * 10 + (**p - '0');
            (*p)++;
        }
        if (n < 1) n = 1;
        *line = n - 1;
        return true;
    }
    return false;
}

static bool parse_ex_range(Editor *e, char **p, int *line0, int *line1) {
    char *s = *p;
    int lc = line_count(&e->text);
    if (*s == '%') {
        *line0 = 0;
        *line1 = lc - 1;
        s++;
    } else if (parse_ex_address(e, &s, line0)) {
        *line1 = *line0;
        if (*s == ',') {
            s++;
            if (!parse_ex_address(e, &s, line1)) return false;
        }
    } else {
        *line0 = byte_line(&e->text, e->cursor);
        *line1 = *line0;
    }
    if (*line0 < 0) *line0 = 0;
    if (*line1 < 0) *line1 = 0;
    if (*line0 >= lc) *line0 = lc - 1;
    if (*line1 >= lc) *line1 = lc - 1;
    if (*line0 > *line1) {
        int t = *line0;
        *line0 = *line1;
        *line1 = t;
    }
    *p = trim_command(s);
    return true;
}

static bool editor_ex_delete_lines(Editor *e, int line0, int line1) {
    if (line_count(&e->text) <= 0) return false;
    size_t a = line_start_by_number(&e->text, line0);
    size_t b = line_start_by_number(&e->text, line1);
    editor_delete_range(e, a, b, true);
    return true;
}

static bool editor_ex_yank_lines(Editor *e, int line0, int line1) {
    size_t a = line_start_by_number(&e->text, line0);
    size_t b = line_end_at(&e->text, line_start_by_number(&e->text, line1));
    if (b < e->text.len) b++;
    editor_yank_range(e, a, b);
    e->yank_linewise = true;
    editor_sync_pending_register_flags(e);
    return true;
}

static bool split_substitute(char *cmd, char **pat, char **rep, bool *global) {
    if (cmd[0] != 's' || cmd[1] != '/') return false;
    char *p = cmd + 2;
    char *slash = strchr(p, '/');
    if (!slash) return false;
    *slash = 0;
    char *r = slash + 1;
    slash = strchr(r, '/');
    if (!slash) return false;
    *slash = 0;
    *pat = p;
    *rep = r;
    *global = strchr(slash + 1, 'g') != NULL;
    return **pat != 0;
}

static size_t expand_replacement(char *dst, size_t dst_size, const char *rep, const char *match, size_t match_len) {
    size_t out = 0;
    for (size_t i = 0; rep[i] && out + 1 < dst_size; i++) {
        if (rep[i] == '&') {
            size_t n = match_len;
            if (n > dst_size - out - 1) n = dst_size - out - 1;
            memcpy(dst + out, match, n);
            out += n;
        } else {
            dst[out++] = rep[i];
        }
    }
    dst[out] = 0;
    return out;
}

static bool editor_replace_regex_in_range(Editor *e, int line0, int line1, const char *pat, const char *rep, bool global) {
    regex_t rx;
    if (regcomp(&rx, pat, REG_EXTENDED) != 0) return false;
    editor_begin_change(e);
    for (int line = line1; line >= line0; line--) {
        size_t start = line_start_by_number(&e->text, line);
        size_t end = line_end_at(&e->text, start);
        size_t p = start;
        while (p <= end) {
            size_t n = end - p;
            char linebuf[4096];
            if (n >= sizeof(linebuf)) n = sizeof(linebuf) - 1;
            memcpy(linebuf, e->text.data + p, n);
            linebuf[n] = 0;
            regmatch_t m;
            if (regexec(&rx, linebuf, 1, &m, 0) != 0 || m.rm_so < 0) break;
            size_t a = p + (size_t)m.rm_so;
            size_t b = p + (size_t)m.rm_eo;
            char repl[4096];
            size_t repl_len = expand_replacement(repl, sizeof(repl), rep, e->text.data + a, b - a);
            text_delete(&e->text, a, b - a);
            text_insert(&e->text, a, repl, repl_len);
            end = end - (b - a) + repl_len;
            p = a + repl_len;
            if (b == a) p++;
            if (!global) break;
        }
    }
    regfree(&rx);
    e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
    e->desired_col = byte_col(&e->text, e->cursor);
    editor_reparse(e);
    return true;
}

static bool line_matches_regex(const Text *t, int line, regex_t *rx) {
    size_t start = line_start_by_number(t, line);
    size_t end = line_end_at(t, start);
    size_t n = end - start;
    char linebuf[4096];
    if (n >= sizeof(linebuf)) n = sizeof(linebuf) - 1;
    memcpy(linebuf, t->data + start, n);
    linebuf[n] = 0;
    return regexec(rx, linebuf, 0, NULL, 0) == 0;
}

static bool editor_global_delete(Editor *e, const char *cmd, bool invert) {
    if ((cmd[0] != 'g' && cmd[0] != 'v') || cmd[1] != '/') return false;
    const char *pat = cmd + 2;
    const char *slash = strchr(pat, '/');
    if (!slash) return false;
    size_t pat_len = (size_t)(slash - pat);
    if (pat_len == 0 || strcmp(slash + 1, "d") != 0) return false;
    char needle[256];
    if (pat_len >= sizeof(needle)) pat_len = sizeof(needle) - 1;
    memcpy(needle, pat, pat_len);
    needle[pat_len] = 0;
    regex_t rx;
    if (regcomp(&rx, needle, REG_EXTENDED) != 0) return false;
    editor_begin_change(e);
    for (int line = line_count(&e->text) - 1; line >= 0; line--) {
        bool hit = line_matches_regex(&e->text, line, &rx);
        if (invert ? !hit : hit) {
            size_t start = line_start_by_number(&e->text, line);
            size_t end = line_end_at(&e->text, start);
            if (end < e->text.len) end++;
            text_delete(&e->text, start, end - start);
        }
    }
    regfree(&rx);
    e->cursor = clamp_cursor_for_normal(&e->text, e->cursor);
    e->desired_col = byte_col(&e->text, e->cursor);
    editor_reparse(e);
    return true;
}

void app_execute_command(App *app) {
    Editor *e = &app->editor;
    editor_store_current_buffer(e);
    char tmp[sizeof(e->command)];
    snprintf(tmp, sizeof(tmp), "%s", e->command);
    char *cmd = trim_command(tmp);
    int ex_line0 = 0, ex_line1 = 0;
    char *ex_tail = cmd;
    bool has_range = parse_ex_range(e, &ex_tail, &ex_line0, &ex_line1);
    if (has_range && strcmp(ex_tail, "d") == 0) {
        editor_ex_delete_lines(e, ex_line0, ex_line1);
    } else if (has_range && strcmp(ex_tail, "y") == 0) {
        editor_ex_yank_lines(e, ex_line0, ex_line1);
        editor_set_status(e, "Yanked");
    } else if (has_range && ex_tail[0] == 's') {
        char *pat = NULL, *rep = NULL;
        bool global = false;
        if (split_substitute(ex_tail, &pat, &rep, &global)) editor_replace_regex_in_range(e, ex_line0, ex_line1, pat, rep, global);
        else editor_set_status(e, "Substitute failed");
    } else if ((cmd[0] == 'g' && editor_global_delete(e, cmd, false)) || (cmd[0] == 'v' && editor_global_delete(e, cmd, true))) {
    } else if (cmd[0] == '/' || cmd[0] == '?') {
        char *pat = cmd + 1;
        snprintf(e->search, sizeof(e->search), "%s", pat);
        e->search_len = strlen(e->search);
        e->search_active = e->search_len > 0;
        int dir = cmd[0] == '?' ? -1 : 1;
        e->last_search_dir = dir;
        editor_repeat_search(e, dir);
    } else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
        if (e->dirty) {
            editor_set_status(e, "No write since last change");
        } else {
            glfwSetWindowShouldClose(app->vk.window, GLFW_TRUE);
        }
    } else if (strcmp(cmd, "q!") == 0 || strcmp(cmd, "quit!") == 0) {
        glfwSetWindowShouldClose(app->vk.window, GLFW_TRUE);
    } else if (strcmp(cmd, "w") == 0 || strncmp(cmd, "w ", 2) == 0 || strncmp(cmd, "write ", 6) == 0) {
        char *path = NULL;
        if (strncmp(cmd, "w ", 2) == 0) path = trim_command(cmd + 2);
        if (strncmp(cmd, "write ", 6) == 0) path = trim_command(cmd + 6);
        if (editor_save_current(e, path)) editor_set_status(e, "Written");
        else editor_set_status(e, "Write failed");
    } else if (strcmp(cmd, "wq") == 0) {
        if (editor_save_current(e, NULL)) glfwSetWindowShouldClose(app->vk.window, GLFW_TRUE);
    } else if (strncmp(cmd, "e ", 2) == 0 || strncmp(cmd, "edit ", 5) == 0) {
        char *path = trim_command(cmd + (cmd[0] == 'e' ? 2 : 5));
        if (!editor_open_buffer(e, path)) editor_set_status(e, "Edit failed");
    } else if (strcmp(cmd, "sp") == 0 || strcmp(cmd, "split") == 0 || strncmp(cmd, "sp ", 3) == 0 || strncmp(cmd, "split ", 6) == 0) {
        char *path = NULL;
        if (strncmp(cmd, "sp ", 3) == 0) path = trim_command(cmd + 3);
        else if (strncmp(cmd, "split ", 6) == 0) path = trim_command(cmd + 6);
        editor_split_current(e, false, path);
    } else if (strcmp(cmd, "vsp") == 0 || strcmp(cmd, "vsplit") == 0 || strncmp(cmd, "vsp ", 4) == 0 || strncmp(cmd, "vsplit ", 7) == 0) {
        char *path = NULL;
        if (strncmp(cmd, "vsp ", 4) == 0) path = trim_command(cmd + 4);
        else if (strncmp(cmd, "vsplit ", 7) == 0) path = trim_command(cmd + 7);
        editor_split_current(e, true, path);
    } else if (strcmp(cmd, "close") == 0 || strcmp(cmd, "clo") == 0) {
        editor_close_view(e, false);
    } else if (strcmp(cmd, "only") == 0 || strcmp(cmd, "on") == 0) {
        editor_close_view(e, true);
    } else if (strncmp(cmd, "wincmd ", 7) == 0) {
        char *arg = trim_command(cmd + 7);
        if (arg[0]) editor_focus_view_direction(e, arg[0]);
    } else if (strcmp(cmd, "tabnew") == 0 || strncmp(cmd, "tabnew ", 7) == 0) {
        char *path = strncmp(cmd, "tabnew ", 7) == 0 ? trim_command(cmd + 7) : NULL;
        editor_tab_new(e, path);
    } else if (strncmp(cmd, "tabedit ", 8) == 0 || strncmp(cmd, "tabe ", 5) == 0) {
        char *path = trim_command(cmd + (cmd[3] == 'e' && cmd[4] == ' ' ? 5 : 8));
        editor_tab_new(e, path);
    } else if (strcmp(cmd, "tabnext") == 0 || strcmp(cmd, "tabn") == 0) {
        editor_tab_switch(e, 1);
    } else if (strcmp(cmd, "tabprev") == 0 || strcmp(cmd, "tabp") == 0) {
        editor_tab_switch(e, -1);
    } else if (strcmp(cmd, "tabclose") == 0 || strcmp(cmd, "tabc") == 0) {
        editor_tab_close(e);
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
    } else if (strcmp(cmd, "bfirst") == 0) {
        editor_load_buffer(e, 0);
    } else if (strcmp(cmd, "blast") == 0) {
        editor_load_buffer(e, e->buffer_count - 1);
    } else if (strncmp(cmd, "buffer ", 7) == 0 || strncmp(cmd, "b ", 2) == 0) {
        char *arg = trim_command(cmd + (cmd[0] == 'b' && cmd[1] == ' ' ? 2 : 7));
        int n = atoi(arg);
        if (n >= 1 && (size_t)n <= e->buffer_count) editor_load_buffer(e, (size_t)n - 1);
        else editor_set_status(e, "No such buffer");
    } else if (strcmp(cmd, "bd") == 0 || strcmp(cmd, "bdelete") == 0) {
        editor_delete_current_buffer(e, false);
    } else if (strcmp(cmd, "bd!") == 0 || strcmp(cmd, "bdelete!") == 0) {
        editor_delete_current_buffer(e, true);
    } else if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "buffers") == 0) {
        char msg[EDITOR_STATUS_MAX] = "";
        for (size_t i = 0; i < e->buffer_count; i++) {
            char part[128];
            const char *name = e->buffers[i].path[0] ? e->buffers[i].path : "[No Name]";
            snprintf(part, sizeof(part), "%s%zu:%s%.90s", i ? "\n" : "", i + 1, e->buffers[i].dirty ? "+" : "", name);
            if (strlen(msg) + strlen(part) + 1 < sizeof(msg)) strcat(msg, part);
        }
        editor_set_status(e, msg);
    } else if (strncmp(cmd, "set ", 4) == 0) {
        char *opt = trim_command(cmd + 4);
        if (strcmp(opt, "number") == 0) e->show_number = true;
        else if (strcmp(opt, "nonumber") == 0) e->show_number = false;
        else if (strcmp(opt, "relativenumber") == 0 || strcmp(opt, "rnu") == 0) e->relative_number = true;
        else if (strcmp(opt, "norelativenumber") == 0 || strcmp(opt, "nornu") == 0) e->relative_number = false;
        else editor_set_status(e, "Unknown option");
    } else {
        editor_set_status(e, "Not an editor command");
    }
    e->mode = MODE_NORMAL;
    e->command_len = 0;
    e->command[0] = 0;
    e->pending = 0;
}
