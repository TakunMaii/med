#include "med.h"

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
    test_split_creates_independent_view();
    test_tab_new_creates_tab();
    test_text_line_index_updates();
    return 0;
}
