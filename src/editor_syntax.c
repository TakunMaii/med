#include "med.h"

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

HighlightKind highlight_at(const Editor *e, size_t byte) {
    size_t lo = 0, hi = e->highlights.len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (e->highlights.spans[mid].start <= byte) lo = mid + 1;
        else hi = mid;
    }
    while (lo > 0) {
        HighlightSpan s = e->highlights.spans[--lo];
        if (byte >= s.start && byte < s.end) return s.kind;
        if (s.end <= byte) break;
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

void editor_reparse(Editor *e) {
    highlights_clear(&e->highlights);
    if (e->text.len > MED_PARSE_MAX_BYTES) {
        if (e->tree) {
            ts_tree_delete(e->tree);
            e->tree = NULL;
        }
        return;
    }
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

void editor_init_treesitter(Editor *e) {
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
