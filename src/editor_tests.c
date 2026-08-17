#include "med.h"
#include "keymap.h"

#include <fcntl.h>
#include <unistd.h>

static void reset_editor_text(Editor *e, const char *text, size_t cursor) {
    text_set(&e->text, text, strlen(text));
    e->cursor = cursor;
    e->mode = MODE_NORMAL;
    e->pending = 0;
    e->operator_pending = 0;
    e->count = 0;
    e->operator_count = 0;
    e->waiting_char = 0;
    e->visual_line = false;
    e->visual_block = false;
    e->desired_col = byte_col(&e->text, e->cursor);
}

static void execute_command(Editor *e, const char *cmd) {
    App app;
    memset(&app, 0, sizeof(app));
    app.editor = *e;
    snprintf(app.editor.command, sizeof(app.editor.command), "%s", cmd);
    app.editor.command_len = strlen(app.editor.command);
    app_execute_command(&app);
    *e = app.editor;
}

static void test_dw_keeps_next_word(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abc def\n", 0);

    editor_key(&e, GLFW_KEY_D, 0, 20);
    editor_key(&e, GLFW_KEY_W, 0, 20);

    assert(strcmp(e.text.data, "def\n") == 0);
    assert(e.cursor == 0);
}

static void test_dw_stops_before_punctuation_word(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abc(def)\n", 0);

    editor_key(&e, GLFW_KEY_D, 0, 20);
    editor_key(&e, GLFW_KEY_W, 0, 20);

    assert(strcmp(e.text.data, "(def)\n") == 0);
    assert(e.cursor == 0);
}

static void test_cw_changes_only_current_word(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abc def\n", 0);

    editor_key(&e, GLFW_KEY_C, 0, 20);
    editor_key(&e, GLFW_KEY_W, 0, 20);

    assert(strcmp(e.text.data, " def\n") == 0);
    assert(e.cursor == 0);
    assert(e.mode == MODE_INSERT);
}

static void test_delete_inside_double_quotes(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "printf(\"abc\");", 9);

    editor_key(&e, GLFW_KEY_D, 0, 20);
    editor_key(&e, GLFW_KEY_I, 0, 20);
    editor_handle_waiting_char(&e, '"');

    assert(strcmp(e.text.data, "printf(\"\");") == 0);
    assert(strcmp(e.yank.data, "abc") == 0);
}

static void test_delete_inside_braces(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "if (x) { foo(); }", 11);

    editor_key(&e, GLFW_KEY_D, 0, 20);
    editor_key(&e, GLFW_KEY_I, 0, 20);
    editor_handle_waiting_char(&e, '{');

    assert(strcmp(e.text.data, "if (x) {}") == 0);
    assert(strcmp(e.yank.data, " foo(); ") == 0);
}

static void test_delete_inside_parens_with_real_char_sequence(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "call(abc);", 6);

    editor_key(&e, GLFW_KEY_D, 0, 20);
    editor_key(&e, GLFW_KEY_I, 0, 20);
    assert(e.suppress_next_char);
    e.suppress_next_char = false;
    editor_handle_waiting_char(&e, '(');

    assert(strcmp(e.text.data, "call();") == 0);
    assert(strcmp(e.yank.data, "abc") == 0);
}

static void test_change_inside_parens_with_real_char_sequence(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "call(abc);", 6);

    editor_key(&e, GLFW_KEY_C, 0, 20);
    editor_key(&e, GLFW_KEY_I, 0, 20);
    assert(e.suppress_next_char);
    e.suppress_next_char = false;
    editor_handle_waiting_char(&e, '(');

    assert(strcmp(e.text.data, "call();") == 0);
    assert(e.mode == MODE_INSERT);
}

static void test_change_inside_brackets(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "items[abc]", 7);

    editor_key(&e, GLFW_KEY_C, 0, 20);
    editor_key(&e, GLFW_KEY_I, 0, 20);
    editor_handle_waiting_char(&e, '[');

    assert(strcmp(e.text.data, "items[]") == 0);
    assert(e.mode == MODE_INSERT);
}

static void test_explicit_one_g_goes_to_first_line(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "a\nb\nc\n", 4);

    editor_key(&e, GLFW_KEY_1, 0, 20);
    editor_key(&e, GLFW_KEY_G, GLFW_MOD_SHIFT, 20);

    assert(e.cursor == 0);
}

static void test_big_word_motions_cross_punctuation(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abc(def) ghi\n", 0);

    editor_key(&e, GLFW_KEY_W, GLFW_MOD_SHIFT, 20);
    assert(e.cursor == 9);
    editor_key(&e, GLFW_KEY_B, GLFW_MOD_SHIFT, 20);
    assert(e.cursor == 0);
    editor_key(&e, GLFW_KEY_E, GLFW_MOD_SHIFT, 20);
    assert(e.cursor == 7);
}

static void test_ge_and_g_underscore_motions(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abc def   \n", 8);

    editor_key(&e, GLFW_KEY_G, 0, 20);
    editor_key(&e, GLFW_KEY_E, 0, 20);
    assert(e.cursor == 6);
    editor_key(&e, GLFW_KEY_G, 0, 20);
    editor_key(&e, GLFW_KEY_MINUS, GLFW_MOD_SHIFT, 20);
    assert(e.cursor == 6);
}

static void test_pipe_motion_uses_count_as_column(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abcdef\n", 0);

    editor_key(&e, GLFW_KEY_4, 0, 20);
    editor_key(&e, GLFW_KEY_BACKSLASH, GLFW_MOD_SHIFT, 20);
    assert(e.cursor == 3);
}

static void test_operator_with_g_prefix(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "one\ntwo\nthree\n", 4);

    editor_key(&e, GLFW_KEY_D, 0, 20);
    editor_key(&e, GLFW_KEY_G, 0, 20);
    editor_key(&e, GLFW_KEY_G, 0, 20);

    assert(strcmp(e.text.data, "three\n") == 0);
    assert(e.cursor == 0);
}

static void test_insert_undo_is_grouped(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abc\n", 1);

    e.mode = MODE_INSERT;
    editor_insert_char(&e, 'X');
    editor_insert_char(&e, 'Y');
    editor_key(&e, GLFW_KEY_ESCAPE, 0, 20);
    assert(strcmp(e.text.data, "aXYbc\n") == 0);

    editor_key(&e, GLFW_KEY_U, 0, 20);
    assert(strcmp(e.text.data, "abc\n") == 0);
}

static void test_repeat_insert_text(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abc\n", 1);

    e.mode = MODE_INSERT;
    editor_insert_char(&e, 'X');
    editor_insert_char(&e, 'Y');
    editor_key(&e, GLFW_KEY_ESCAPE, 0, 20);
    editor_key(&e, GLFW_KEY_L, 0, 20);
    editor_key(&e, GLFW_KEY_PERIOD, 0, 20);

    assert(strcmp(e.text.data, "aXYXYbc\n") == 0);
}

static void test_visual_delete_updates_yank(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abcde", 3);
    e.mode = MODE_VISUAL;
    e.visual_anchor = 1;

    editor_key(&e, GLFW_KEY_D, 0, 20);

    assert(strcmp(e.text.data, "ae") == 0);
    assert(e.has_yank);
    assert(!e.yank_linewise);
    assert(strcmp(e.yank.data, "bcd") == 0);
}

static void test_visual_o_swaps_selection_endpoints(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abcde", 1);

    editor_key(&e, GLFW_KEY_V, 0, 20);
    editor_key(&e, GLFW_KEY_L, 0, 20);
    editor_key(&e, GLFW_KEY_L, 0, 20);
    assert(e.cursor == 3);
    assert(e.visual_anchor == 1);
    editor_key(&e, GLFW_KEY_O, 0, 20);
    assert(e.cursor == 1);
    assert(e.visual_anchor == 3);
}

static void test_gv_restores_last_visual_selection(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abcde", 1);

    editor_key(&e, GLFW_KEY_V, 0, 20);
    editor_key(&e, GLFW_KEY_L, 0, 20);
    editor_key(&e, GLFW_KEY_L, 0, 20);
    editor_key(&e, GLFW_KEY_ESCAPE, 0, 20);
    editor_key(&e, GLFW_KEY_G, 0, 20);
    editor_key(&e, GLFW_KEY_V, 0, 20);

    assert(e.mode == MODE_VISUAL);
    assert(e.visual_anchor == 1);
    assert(e.cursor == 3);
}

static void test_ls_status_is_multiline(void) {
    App app;
    memset(&app, 0, sizeof(app));
    editor_init(&app.editor, NULL);
    snprintf(app.editor.command, sizeof(app.editor.command), "bnew");
    app.editor.command_len = strlen(app.editor.command);
    app_execute_command(&app);
    snprintf(app.editor.command, sizeof(app.editor.command), "ls");
    app.editor.command_len = strlen(app.editor.command);
    app_execute_command(&app);

    assert(strchr(app.editor.status, '\n') != NULL);
}

static void test_delete_inside_big_word(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "foo(bar) baz\n", 2);

    editor_key(&e, GLFW_KEY_D, 0, 20);
    editor_key(&e, GLFW_KEY_I, 0, 20);
    editor_handle_waiting_char(&e, 'W');

    assert(strcmp(e.text.data, " baz\n") == 0);
    assert(strcmp(e.yank.data, "foo(bar)") == 0);
}

static void test_yank_inside_paragraph(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "one\ntwo\n\nthree\n", 5);

    editor_key(&e, GLFW_KEY_Y, 0, 20);
    editor_key(&e, GLFW_KEY_I, 0, 20);
    editor_handle_waiting_char(&e, 'p');

    assert(strcmp(e.yank.data, "one\ntwo\n") == 0);
}

static void test_toggle_case_count(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "aBcD\n", 0);

    editor_key(&e, GLFW_KEY_3, 0, 20);
    editor_key(&e, GLFW_KEY_GRAVE_ACCENT, GLFW_MOD_SHIFT, 20);

    assert(strcmp(e.text.data, "AbCD\n") == 0);
    assert(e.cursor == 2);
}

static void test_marks_exact_and_linewise_jump(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "one\n  two\nthree\n", 6);

    editor_key(&e, GLFW_KEY_M, 0, 20);
    editor_handle_waiting_char(&e, 'a');
    editor_key(&e, GLFW_KEY_G, GLFW_MOD_SHIFT, 20);
    editor_key(&e, GLFW_KEY_APOSTROPHE, 0, 20);
    editor_handle_waiting_char(&e, 'a');
    assert(e.cursor == 6);
    editor_key(&e, GLFW_KEY_G, GLFW_MOD_SHIFT, 20);
    editor_key(&e, GLFW_KEY_GRAVE_ACCENT, 0, 20);
    editor_handle_waiting_char(&e, 'a');
    assert(e.cursor == 6);
}

static void test_named_register_yank_and_paste(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "one\ntwo\n", 0);

    editor_key(&e, GLFW_KEY_APOSTROPHE, GLFW_MOD_SHIFT, 20);
    editor_handle_waiting_char(&e, 'a');
    editor_key(&e, GLFW_KEY_Y, 0, 20);
    editor_key(&e, GLFW_KEY_Y, 0, 20);
    editor_key(&e, GLFW_KEY_G, GLFW_MOD_SHIFT, 20);
    editor_key(&e, GLFW_KEY_APOSTROPHE, GLFW_MOD_SHIFT, 20);
    editor_handle_waiting_char(&e, 'a');
    editor_key(&e, GLFW_KEY_P, 0, 20);

    assert(strcmp(e.text.data, "one\ntwo\none\n") == 0);
}

static void test_ex_range_delete_and_yank(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "one\ntwo\nthree\n", 0);

    execute_command(&e, "1,2d");
    assert(strcmp(e.text.data, "three\n") == 0);
    assert(strcmp(e.yank.data, "one\ntwo\n") == 0);

    execute_command(&e, "%y");
    assert(e.yank_linewise);
    assert(strcmp(e.yank.data, "three\n") == 0);
}

static void test_ex_substitute_literal_global_flag(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "foo foo\nfoo\n", 0);

    execute_command(&e, "%s/foo/bar/g");

    assert(strcmp(e.text.data, "bar bar\nbar\n") == 0);
}

static void test_ex_global_and_inverse_delete(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "keep\nzap\nkeep too\n", 0);

    execute_command(&e, "g/zap/d");
    assert(strcmp(e.text.data, "keep\nkeep too\n") == 0);
    execute_command(&e, "v/too/d");
    assert(strcmp(e.text.data, "keep too\n") == 0);
}

static void test_visual_block_replace(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abc\ndef\n", 1);

    editor_key(&e, GLFW_KEY_V, GLFW_MOD_CONTROL, 20);
    editor_key(&e, GLFW_KEY_DOWN, 0, 20);
    editor_key(&e, GLFW_KEY_RIGHT, 0, 20);
    editor_key(&e, GLFW_KEY_R, 0, 20);
    editor_handle_waiting_char(&e, 'X');

    assert(strcmp(e.text.data, "aXX\ndXX\n") == 0);
    assert(e.mode == MODE_NORMAL);
}

static void test_gt_and_gT_switch_tabs(void) {
    Editor e;
    editor_init(&e, NULL);
    editor_tab_new(&e, NULL);
    assert(e.current_tab == 1);

    editor_key(&e, GLFW_KEY_G, 0, 20);
    editor_key(&e, GLFW_KEY_T, GLFW_MOD_SHIFT, 20);
    assert(e.current_tab == 0);
    editor_key(&e, GLFW_KEY_G, 0, 20);
    editor_key(&e, GLFW_KEY_T, 0, 20);
    assert(e.current_tab == 1);
}

static void test_ctrl_w_cycle_and_quit_window(void) {
    Editor e;
    editor_init(&e, NULL);
    editor_split_current(&e, true, NULL);
    assert(e.tabs[0].view_count == 2);
    assert(e.tabs[0].active_view == 1);

    editor_key(&e, GLFW_KEY_W, GLFW_MOD_CONTROL, 20);
    editor_key(&e, GLFW_KEY_W, 0, 20);
    assert(e.tabs[0].active_view == 0);
    editor_key(&e, GLFW_KEY_W, GLFW_MOD_CONTROL, 20);
    editor_key(&e, GLFW_KEY_Q, 0, 20);
    assert(e.tabs[0].view_count == 1);
}

static void test_macro_record_and_replay(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abc\n", 0);

    editor_key(&e, GLFW_KEY_Q, 0, 20);
    editor_handle_waiting_char(&e, 'a');
    editor_key(&e, GLFW_KEY_X, 0, 20);
    editor_key(&e, GLFW_KEY_Q, 0, 20);
    editor_key(&e, GLFW_KEY_2, GLFW_MOD_SHIFT, 20);
    editor_handle_waiting_char(&e, 'a');
    editor_key(&e, GLFW_KEY_2, GLFW_MOD_SHIFT, 20);
    editor_handle_waiting_char(&e, '@');

    assert(strcmp(e.text.data, "\n") == 0);
}

static void test_dot_replays_operator_change(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "one two\n", 0);

    editor_key(&e, GLFW_KEY_D, 0, 20);
    editor_key(&e, GLFW_KEY_W, 0, 20);
    editor_key(&e, GLFW_KEY_PERIOD, 0, 20);

    assert(strcmp(e.text.data, "") == 0);
}

static void test_visual_block_insert_and_append(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "aa\nbb\n", 0);

    editor_key(&e, GLFW_KEY_V, GLFW_MOD_CONTROL, 20);
    editor_key(&e, GLFW_KEY_DOWN, 0, 20);
    editor_key(&e, GLFW_KEY_I, GLFW_MOD_SHIFT, 20);
    editor_insert_char(&e, '>');
    editor_key(&e, GLFW_KEY_ESCAPE, 0, 20);
    assert(strcmp(e.text.data, ">aa\n>bb\n") == 0);

    editor_key(&e, GLFW_KEY_V, GLFW_MOD_CONTROL, 20);
    editor_key(&e, GLFW_KEY_DOWN, 0, 20);
    editor_key(&e, GLFW_KEY_A, GLFW_MOD_SHIFT, 20);
    editor_insert_char(&e, '<');
    editor_key(&e, GLFW_KEY_ESCAPE, 0, 20);
    assert(strcmp(e.text.data, ">aa<\n>bb<\n") == 0);
}

static void test_regex_substitute_and_global(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "foo1\nfoo22\nbar\n", 0);

    execute_command(&e, "%s/foo[0-9]+/X&/g");
    assert(strcmp(e.text.data, "Xfoo1\nXfoo22\nbar\n") == 0);
    execute_command(&e, "g/^Xfoo[0-9]+$/d");
    assert(strcmp(e.text.data, "bar\n") == 0);
}

static void test_gd_and_gD_request_lsp_before_delete_operator(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "int main(void) { return 0; }\n", 4);
    e.lsp.running = true;
    e.lsp.opened = true;
    e.lsp.in_fd = open("/dev/null", O_WRONLY);
    assert(e.lsp.in_fd >= 0);

    editor_key(&e, GLFW_KEY_G, 0, 20);
    editor_key(&e, GLFW_KEY_D, 0, 20);
    assert(e.operator_pending == 0);
    assert(e.pending == 0);
    assert(e.lsp.definition_id > 0);

    editor_key(&e, GLFW_KEY_G, 0, 20);
    editor_key(&e, GLFW_KEY_D, GLFW_MOD_SHIFT, 20);
    assert(e.operator_pending == 0);
    assert(e.pending == 0);
    assert(e.lsp.declaration_id > 0);
    close(e.lsp.in_fd);
}

static void test_lsp_completion_selection_scrolls_visible_window(void) {
    Editor e;
    editor_init(&e, NULL);
    e.lsp.completion_visible = true;
    e.lsp.completion_count = 12;
    e.lsp.completion_selected = 0;
    e.lsp.completion_scroll = 0;

    for (int i = 0; i < 8; i++) lsp_completion_move(&e, 1);
    assert(e.lsp.completion_selected == 8);
    assert(e.lsp.completion_scroll == 1);

    lsp_completion_move(&e, -1);
    assert(e.lsp.completion_selected == 7);
    assert(e.lsp.completion_scroll == 1);

    for (int i = 0; i < 8; i++) lsp_completion_move(&e, -1);
    assert(e.lsp.completion_selected == 11);
    assert(e.lsp.completion_scroll == 4);
}

static void test_lsp_popups_close_on_escape_and_cursor_move(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abc\n", 0);
    e.lsp.hover_visible = true;
    e.lsp.completion_visible = true;

    editor_key(&e, GLFW_KEY_ESCAPE, 0, 20);
    assert(!e.lsp.hover_visible);
    assert(!e.lsp.completion_visible);

    e.lsp.hover_visible = true;
    e.lsp.completion_visible = true;
    editor_record_cursor_if_moved(&e, 0, 0, MODE_NORMAL, 0.0);
    assert(e.lsp.hover_visible);
    assert(e.lsp.completion_visible);
    editor_key(&e, GLFW_KEY_L, 0, 20);
    editor_record_cursor_if_moved(&e, 0, 0, MODE_NORMAL, 0.0);
    assert(!e.lsp.hover_visible);
    assert(!e.lsp.completion_visible);
}

static void test_lsp_completion_request_clears_stale_items(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abc\n", 0);
    e.lsp.running = true;
    e.lsp.opened = true;
    e.lsp.in_fd = open("/dev/null", O_WRONLY);
    assert(e.lsp.in_fd >= 0);
    e.lsp.completion_visible = true;
    e.lsp.completion_count = 3;
    e.lsp.completion_selected = 2;
    e.lsp.completion_scroll = 1;

    lsp_request_completion(&e);
    assert(!e.lsp.completion_visible);
    assert(e.lsp.completion_count == 0);
    assert(e.lsp.completion_selected == 0);
    assert(e.lsp.completion_scroll == 0);
    close(e.lsp.in_fd);
}

static void test_insert_completion_queues_after_buffer_change(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "pri\n", 3);
    e.mode = MODE_INSERT;
    e.lsp.completion_visible = true;
    e.lsp.completion_count = 2;
    e.lsp.completion_selected = 1;
    e.lsp.completion_scroll = 1;

    editor_insert_char(&e, 'n');
    assert(strcmp(e.text.data, "prin\n") == 0);
    assert(e.lsp.needs_sync);
    assert(e.lsp.completion_pending);
    assert(!e.lsp.completion_visible);
    assert(e.lsp.completion_count == 0);
    assert(e.lsp.completion_selected == 0);
    assert(e.lsp.completion_scroll == 0);
    assert(e.lsp.completion_trigger_kind == 1);
    assert(e.lsp.completion_trigger[0] == 0);
    assert(e.lsp.completion_id == 0);

    e.lsp.needs_sync = false;
    e.lsp.completion_pending = false;
    editor_insert_char(&e, 't');
    assert(strcmp(e.text.data, "print\n") == 0);
    assert(e.lsp.needs_sync);
    assert(e.lsp.completion_pending);

    editor_insert_char(&e, '.');
    assert(e.lsp.completion_pending);
    assert(e.lsp.completion_trigger_kind == 2);
    assert(strcmp(e.lsp.completion_trigger, ".") == 0);
}

static void test_pending_completion_waits_for_synced_buffer(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "prin\n", 4);
    e.lsp.running = true;
    e.lsp.initialized = true;
    e.lsp.opened = true;
    e.lsp.needs_sync = true;
    e.lsp.completion_pending = true;
    e.lsp.in_fd = open("/dev/null", O_WRONLY);
    assert(e.lsp.in_fd >= 0);

    lsp_request_pending_completion(&e);
    assert(e.lsp.completion_pending);
    assert(e.lsp.completion_id == 0);

    e.lsp.needs_sync = false;
    e.lsp.completion_trigger_kind = 2;
    snprintf(e.lsp.completion_trigger, sizeof(e.lsp.completion_trigger), ".");
    lsp_request_pending_completion(&e);
    assert(!e.lsp.completion_pending);
    assert(e.lsp.completion_id != 0);
    assert(e.lsp.completion_trigger_kind == 0);
    assert(e.lsp.completion_trigger[0] == 0);
    close(e.lsp.in_fd);
}

static void test_lsp_completion_accept_replaces_typed_prefix(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "pri\n", 3);
    e.mode = MODE_INSERT;
    e.lsp.running = true;
    e.lsp.opened = true;
    e.lsp.in_fd = open("/dev/null", O_WRONLY);
    assert(e.lsp.in_fd >= 0);
    e.lsp.completion_visible = true;
    e.lsp.completion_count = 1;
    e.lsp.completion_selected = 0;
    snprintf(e.lsp.completions[0], sizeof(e.lsp.completions[0]), "printf");

    assert(lsp_completion_accept(&e));
    assert(strcmp(e.text.data, "printf\n") == 0);
    assert(!e.lsp.completion_visible);
    assert(e.lsp.completion_count == 0);
    assert(e.lsp.completion_id == 0);
    close(e.lsp.in_fd);
}

static void test_lsp_completion_accept_uses_insert_text_not_label(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "mem\n", 3);
    e.mode = MODE_INSERT;
    e.lsp.completion_visible = true;
    e.lsp.completion_count = 1;
    e.lsp.completion_selected = 0;
    snprintf(e.lsp.completions[0], sizeof(e.lsp.completions[0]), " memset(void *s, int c, size_t n)");
    snprintf(e.lsp.completion_insert_texts[0], sizeof(e.lsp.completion_insert_texts[0]), "memset");

    assert(lsp_completion_accept(&e));
    assert(strcmp(e.text.data, "memset\n") == 0);
    assert(!e.lsp.completion_visible);
    assert(e.lsp.completion_count == 0);
}

static void test_insert_tab_aligns_to_shiftwidth(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "  x\n", 2);
    e.mode = MODE_INSERT;

    editor_key(&e, GLFW_KEY_TAB, 0, 20);
    assert(strcmp(e.text.data, "    x\n") == 0);
    assert(e.cursor == 4);

    editor_key(&e, GLFW_KEY_TAB, 0, 20);
    assert(strcmp(e.text.data, "        x\n") == 0);
    assert(e.cursor == 8);
}

static void test_insert_enter_autoindents(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "    foo\n", 7);
    e.mode = MODE_INSERT;

    editor_key(&e, GLFW_KEY_ENTER, 0, 20);
    assert(strcmp(e.text.data, "    foo\n    \n") == 0);
    assert(e.cursor == 12);
}

static void test_o_and_O_autoindent(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "    foo\nbar\n", 4);

    editor_key(&e, GLFW_KEY_O, 0, 20);
    assert(strcmp(e.text.data, "    foo\n    \nbar\n") == 0);
    assert(e.mode == MODE_INSERT);
    assert(e.cursor == 12);

    reset_editor_text(&e, "    foo\nbar\n", 4);
    editor_key(&e, GLFW_KEY_O, GLFW_MOD_SHIFT, 20);
    assert(strcmp(e.text.data, "    \n    foo\nbar\n") == 0);
    assert(e.mode == MODE_INSERT);
    assert(e.cursor == 4);
}

static void test_ctrl_o_returns_to_previous_jump(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "one\ntwo\nthree\n", 0);

    editor_push_jump(&e);
    e.cursor = line_start_by_number(&e.text, 2);
    e.desired_col = byte_col(&e.text, e.cursor);
    editor_key(&e, GLFW_KEY_O, GLFW_MOD_CONTROL, 20);

    assert(e.cursor == 0);
}

static void test_visual_block_delete(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abc\ndef\n", 1);

    editor_key(&e, GLFW_KEY_V, GLFW_MOD_CONTROL, 20);
    editor_key(&e, GLFW_KEY_DOWN, 0, 20);
    editor_key(&e, GLFW_KEY_RIGHT, 0, 20);
    editor_key(&e, GLFW_KEY_D, 0, 20);

    assert(strcmp(e.text.data, "a\nd\n") == 0);
    assert(e.has_yank);
    assert(e.yank_blockwise);
    assert(strcmp(e.yank.data, "bc\nef") == 0);
}

static void test_split_creates_independent_view(void) {
    Editor e;
    editor_init(&e, NULL);
    reset_editor_text(&e, "abc\ndef\n", 0);

    editor_split_current(&e, true, NULL);
    assert(e.tab_count == 1);
    assert(e.tabs[0].view_count == 2);
    assert(e.tabs[0].nodes[e.tabs[0].root].kind == SPLIT_COL);
    assert(e.tabs[0].active_view == 1);
}

static void test_tab_new_creates_tab(void) {
    Editor e;
    editor_init(&e, NULL);
    size_t old_buffer = e.tabs[0].views[0].buffer_index;

    editor_tab_new(&e, NULL);
    assert(e.tab_count == 2);
    assert(e.current_tab == 1);
    assert(e.tabs[1].view_count == 1);
    assert(e.tabs[0].views[0].buffer_index == old_buffer);
    assert(e.tabs[1].views[0].buffer_index != old_buffer);
}

static void test_text_line_index_updates(void) {
    Text t;
    text_init(&t);
    text_set(&t, "aa\nbbb\nc", 8);
    assert(line_count(&t) == 3);
    assert(byte_line(&t, 4) == 1);
    assert(byte_col(&t, 4) == 1);
    assert(line_start_by_number(&t, 2) == 7);

    text_insert(&t, 2, "\nxx", 3);
    assert(strcmp(t.data, "aa\nxx\nbbb\nc") == 0);
    assert(line_count(&t) == 4);
    assert(line_start_by_number(&t, 2) == 6);
    assert(byte_line(&t, 7) == 2);

    text_delete(&t, 2, 3);
    assert(strcmp(t.data, "aa\nbbb\nc") == 0);
    assert(line_count(&t) == 3);
    assert(line_start_by_number(&t, 2) == 7);
    text_free(&t);
}

static void test_keymap_prefers_more_specific_binding(void) {
    KeymapBinding bindings[] = {
        {.key = GLFW_KEY_ESCAPE, .priority = 1},
        {.key = GLFW_KEY_ESCAPE, .mode_exact = true, .mode = MODE_INSERT, .priority = 1},
    };
    KeymapProbe probe = {
        .key = GLFW_KEY_ESCAPE,
        .mods = 0,
        .ch = 0,
        .ctx = {.mode = MODE_INSERT},
    };
    assert(keymap_resolve(bindings, 2, &probe, NULL) == 1);
}

int main(void) {
    test_dw_keeps_next_word();
    test_dw_stops_before_punctuation_word();
    test_cw_changes_only_current_word();
    test_delete_inside_double_quotes();
    test_delete_inside_braces();
    test_delete_inside_parens_with_real_char_sequence();
    test_change_inside_parens_with_real_char_sequence();
    test_change_inside_brackets();
    test_explicit_one_g_goes_to_first_line();
    test_big_word_motions_cross_punctuation();
    test_ge_and_g_underscore_motions();
    test_pipe_motion_uses_count_as_column();
    test_operator_with_g_prefix();
    test_insert_undo_is_grouped();
    test_repeat_insert_text();
    test_visual_block_delete();
    test_visual_delete_updates_yank();
    test_visual_o_swaps_selection_endpoints();
    test_gv_restores_last_visual_selection();
    test_ls_status_is_multiline();
    test_delete_inside_big_word();
    test_yank_inside_paragraph();
    test_toggle_case_count();
    test_marks_exact_and_linewise_jump();
    test_named_register_yank_and_paste();
    test_ex_range_delete_and_yank();
    test_ex_substitute_literal_global_flag();
    test_ex_global_and_inverse_delete();
    test_visual_block_replace();
    test_gt_and_gT_switch_tabs();
    test_ctrl_w_cycle_and_quit_window();
    test_macro_record_and_replay();
    test_dot_replays_operator_change();
    test_visual_block_insert_and_append();
    test_regex_substitute_and_global();
    test_gd_and_gD_request_lsp_before_delete_operator();
    test_lsp_completion_selection_scrolls_visible_window();
    test_lsp_popups_close_on_escape_and_cursor_move();
    test_lsp_completion_request_clears_stale_items();
    test_insert_completion_queues_after_buffer_change();
    test_pending_completion_waits_for_synced_buffer();
    test_lsp_completion_accept_replaces_typed_prefix();
    test_lsp_completion_accept_uses_insert_text_not_label();
    test_insert_tab_aligns_to_shiftwidth();
    test_insert_enter_autoindents();
    test_o_and_O_autoindent();
    test_ctrl_o_returns_to_previous_jump();
    test_split_creates_independent_view();
    test_tab_new_creates_tab();
    test_text_line_index_updates();
    test_keymap_prefers_more_specific_binding();
    return 0;
}
