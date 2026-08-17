#include "med.h"
#include "cJSON.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    const char *name;
    const char *cmd;
    const char *const *extensions;
} LspServerConfig;

static const char *const clangd_exts[] = {".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", NULL};
static const LspServerConfig lsp_servers[] = {
    {"clangd", "clangd", clangd_exts},
};

void lsp_init(LspClient *lsp) {
    memset(lsp, 0, sizeof(*lsp));
    lsp->in_fd = -1;
    lsp->out_fd = -1;
    lsp->next_id = 1;
}

void lsp_shutdown(LspClient *lsp) {
    if (lsp->in_fd >= 0) close(lsp->in_fd);
    if (lsp->out_fd >= 0) close(lsp->out_fd);
    if (lsp->pid > 0) {
        kill(lsp->pid, SIGTERM);
        waitpid(lsp->pid, NULL, WNOHANG);
    }
    lsp_init(lsp);
}

static bool has_extension(const char *path, const char *const *exts) {
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    for (size_t i = 0; exts[i]; i++) {
        if (strcmp(dot, exts[i]) == 0) return true;
    }
    return false;
}

static const LspServerConfig *server_for_path(const char *path) {
    if (!path || !path[0]) return NULL;
    for (size_t i = 0; i < sizeof(lsp_servers) / sizeof(lsp_servers[0]); i++) {
        if (has_extension(path, lsp_servers[i].extensions)) return &lsp_servers[i];
    }
    return NULL;
}

static void dirname_of(const char *path, char *out, size_t out_size) {
    snprintf(out, out_size, "%s", path && path[0] ? path : ".");
    char *slash = strrchr(out, '/');
    if (slash) *slash = 0;
    else snprintf(out, out_size, ".");
}

static void find_root(const char *path, char *out, size_t out_size) {
    dirname_of(path, out, out_size);
    char cur[512];
    snprintf(cur, sizeof(cur), "%s", out);
    for (;;) {
        char marker[768];
        snprintf(marker, sizeof(marker), "%s/.clangd", cur);
        if (access(marker, F_OK) == 0) break;
        snprintf(marker, sizeof(marker), "%s/compile_commands.json", cur);
        if (access(marker, F_OK) == 0) break;
        snprintf(marker, sizeof(marker), "%s/.git", cur);
        if (access(marker, F_OK) == 0) break;
        char *slash = strrchr(cur, '/');
        if (!slash || slash == cur) break;
        *slash = 0;
    }
    snprintf(out, out_size, "%s", cur);
}

static void file_uri(const char *path, char *out, size_t out_size) {
    char resolved[1024];
    if (!realpath(path, resolved)) snprintf(resolved, sizeof(resolved), "%s", path);
    snprintf(out, out_size, "file://%s", resolved);
}

static bool lsp_write_json(LspClient *lsp, cJSON *msg) {
    char *body = cJSON_PrintUnformatted(msg);
    if (!body) return false;
    char header[128];
    int hn = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", strlen(body));
    bool ok = true;
    if (write(lsp->in_fd, header, (size_t)hn) < 0) ok = false;
    if (ok && write(lsp->in_fd, body, strlen(body)) < 0) ok = false;
    cJSON_free(body);
    return ok;
}

static cJSON *position_obj(Editor *e) {
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", byte_line(&e->text, e->cursor));
    cJSON_AddNumberToObject(pos, "character", byte_col(&e->text, e->cursor));
    return pos;
}

static cJSON *text_document_obj(Editor *e) {
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", e->lsp.uri);
    return td;
}

static int lsp_send_request(Editor *e, const char *method, cJSON *params) {
    LspClient *lsp = &e->lsp;
    int id = lsp->next_id++;
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(msg, "id", id);
    cJSON_AddStringToObject(msg, "method", method);
    if (params) cJSON_AddItemToObject(msg, "params", params);
    lsp_write_json(lsp, msg);
    cJSON_Delete(msg);
    return id;
}

static void lsp_send_notification(Editor *e, const char *method, cJSON *params) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "jsonrpc", "2.0");
    cJSON_AddStringToObject(msg, "method", method);
    if (params) cJSON_AddItemToObject(msg, "params", params);
    lsp_write_json(&e->lsp, msg);
    cJSON_Delete(msg);
}

static bool spawn_server(Editor *e, const LspServerConfig *cfg) {
    int to_child[2], from_child[2];
    if (pipe(to_child) != 0 || pipe(from_child) != 0) return false;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        execlp(cfg->cmd, cfg->cmd, NULL);
        _exit(127);
    }
    close(to_child[0]);
    close(from_child[1]);
    if (pid < 0) return false;
    fcntl(from_child[0], F_SETFL, fcntl(from_child[0], F_GETFL, 0) | O_NONBLOCK);
    e->lsp.pid = pid;
    e->lsp.in_fd = to_child[1];
    e->lsp.out_fd = from_child[0];
    e->lsp.running = true;
    snprintf(e->lsp.server_name, sizeof(e->lsp.server_name), "%s", cfg->name);
    return true;
}

static void lsp_initialize(Editor *e) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "processId", (double)getpid());
    cJSON_AddStringToObject(params, "rootUri", e->lsp.root);
    cJSON *caps = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "capabilities", caps);
    lsp_send_request(e, "initialize", params);
}

static void lsp_did_open(Editor *e) {
    cJSON *params = cJSON_CreateObject();
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "uri", e->lsp.uri);
    cJSON_AddStringToObject(doc, "languageId", has_extension(e->path, clangd_exts) ? "c" : "plaintext");
    cJSON_AddNumberToObject(doc, "version", ++e->lsp.version);
    cJSON_AddStringToObject(doc, "text", e->text.data);
    cJSON_AddItemToObject(params, "textDocument", doc);
    lsp_send_notification(e, "textDocument/didOpen", params);
    e->lsp.opened = true;
}

void lsp_maybe_start(Editor *e) {
    if (e->lsp.running || !e->path[0]) return;
    const LspServerConfig *cfg = server_for_path(e->path);
    if (!cfg) return;
    lsp_init(&e->lsp);
    char root_path[512];
    find_root(e->path, root_path, sizeof(root_path));
    snprintf(e->lsp.root, sizeof(e->lsp.root), "file://%s", root_path);
    file_uri(e->path, e->lsp.uri, sizeof(e->lsp.uri));
    if (!spawn_server(e, cfg)) return;
    lsp_initialize(e);
}

void lsp_sync_if_needed(Editor *e) {
    if (!e->lsp.running || !e->lsp.initialized) return;
    if (!e->lsp.opened) {
        lsp_did_open(e);
        return;
    }
    if (!e->lsp.needs_sync) return;
    cJSON *params = cJSON_CreateObject();
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "uri", e->lsp.uri);
    cJSON_AddNumberToObject(doc, "version", ++e->lsp.version);
    cJSON_AddItemToObject(params, "textDocument", doc);
    cJSON *changes = cJSON_CreateArray();
    cJSON *change = cJSON_CreateObject();
    cJSON_AddStringToObject(change, "text", e->text.data);
    cJSON_AddItemToArray(changes, change);
    cJSON_AddItemToObject(params, "contentChanges", changes);
    lsp_send_notification(e, "textDocument/didChange", params);
    e->lsp.needs_sync = false;
}

void lsp_request_hover(Editor *e) {
    if (!e->lsp.running) return;
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", text_document_obj(e));
    cJSON_AddItemToObject(params, "position", position_obj(e));
    e->lsp.hover_id = lsp_send_request(e, "textDocument/hover", params);
}

void lsp_request_completion(Editor *e) {
    if (!e->lsp.running || !e->lsp.opened) return;
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", text_document_obj(e));
    cJSON_AddItemToObject(params, "position", position_obj(e));
    e->lsp.completion_id = lsp_send_request(e, "textDocument/completion", params);
}

void lsp_request_definition(Editor *e, bool declaration) {
    if (!e->lsp.running) return;
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", text_document_obj(e));
    cJSON_AddItemToObject(params, "position", position_obj(e));
    int id = lsp_send_request(e, declaration ? "textDocument/declaration" : "textDocument/definition", params);
    if (declaration) e->lsp.declaration_id = id;
    else e->lsp.definition_id = id;
}

static void parse_hover(Editor *e, cJSON *result) {
    if (!result) return;
    cJSON *contents = cJSON_GetObjectItem(result, "contents");
    const char *s = NULL;
    if (cJSON_IsString(contents)) s = contents->valuestring;
    else if (cJSON_IsObject(contents)) {
        cJSON *value = cJSON_GetObjectItem(contents, "value");
        if (cJSON_IsString(value)) s = value->valuestring;
    }
    if (s) {
        snprintf(e->lsp.hover, sizeof(e->lsp.hover), "%s", s);
        e->lsp.hover_visible = true;
    }
}

static const char *completion_label(cJSON *item) {
    cJSON *label = cJSON_GetObjectItem(item, "label");
    return cJSON_IsString(label) ? label->valuestring : NULL;
}

static void parse_completion(Editor *e, cJSON *result) {
    cJSON *items = cJSON_IsArray(result) ? result : cJSON_GetObjectItem(result, "items");
    if (!cJSON_IsArray(items)) return;
    e->lsp.completion_count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, items) {
        if (e->lsp.completion_count >= 32) break;
        const char *label = completion_label(item);
        if (!label) continue;
        size_t i = e->lsp.completion_count++;
        snprintf(e->lsp.completions[i], sizeof(e->lsp.completions[i]), "%s", label);
        cJSON *detail = cJSON_GetObjectItem(item, "detail");
        snprintf(e->lsp.completion_details[i], sizeof(e->lsp.completion_details[i]), "%s", cJSON_IsString(detail) ? detail->valuestring : "");
    }
    e->lsp.completion_selected = 0;
    e->lsp.completion_scroll = 0;
    e->lsp.completion_visible = e->lsp.completion_count > 0;
}

static void uri_to_path(const char *uri, char *out, size_t out_size) {
    if (strncmp(uri, "file://", 7) == 0) snprintf(out, out_size, "%s", uri + 7);
    else snprintf(out, out_size, "%s", uri);
}

static void jump_to_location(Editor *e, cJSON *result) {
    cJSON *loc = cJSON_IsArray(result) ? cJSON_GetArrayItem(result, 0) : result;
    if (!cJSON_IsObject(loc)) return;
    cJSON *uri = cJSON_GetObjectItem(loc, "uri");
    cJSON *range = cJSON_GetObjectItem(loc, "range");
    if (!cJSON_IsString(uri) || !cJSON_IsObject(range)) return;
    cJSON *start = cJSON_GetObjectItem(range, "start");
    cJSON *line = cJSON_GetObjectItem(start, "line");
    cJSON *ch = cJSON_GetObjectItem(start, "character");
    int target_line = cJSON_IsNumber(line) ? line->valueint : 0;
    int target_col = cJSON_IsNumber(ch) ? ch->valueint : 0;
    char path[1024];
    uri_to_path(uri->valuestring, path, sizeof(path));
    if (strcmp(path, e->path) != 0) {
        editor_open_buffer(e, path);
        file_uri(e->path, e->lsp.uri, sizeof(e->lsp.uri));
        e->lsp.opened = false;
    }
    size_t pos = line_start_by_number(&e->text, target_line) + (size_t)(target_col < 0 ? 0 : target_col);
    size_t end = line_end_at(&e->text, line_start_by_number(&e->text, target_line));
    if (pos > end) pos = end;
    e->cursor = clamp_cursor_for_normal(&e->text, pos);
    e->desired_col = byte_col(&e->text, e->cursor);
    snprintf(e->status, sizeof(e->status), "LSP: jumped to %.400s:%d", path, target_line + 1);
}

static void parse_diagnostics(Editor *e, cJSON *params) {
    cJSON *items = cJSON_GetObjectItem(params, "diagnostics");
    if (!cJSON_IsArray(items)) return;
    e->lsp.diagnostic_count = 0;
    cJSON *d = NULL;
    cJSON_ArrayForEach(d, items) {
        cJSON *severity = cJSON_GetObjectItem(d, "severity");
        int sev = cJSON_IsNumber(severity) ? severity->valueint : 1;
        if (sev != 1 || e->lsp.diagnostic_count >= 64) continue;
        LspDiagnostic *out = &e->lsp.diagnostics[e->lsp.diagnostic_count++];
        out->severity = sev;
        cJSON *message = cJSON_GetObjectItem(d, "message");
        snprintf(out->message, sizeof(out->message), "%s", cJSON_IsString(message) ? message->valuestring : "");
        cJSON *range = cJSON_GetObjectItem(d, "range");
        cJSON *start = cJSON_GetObjectItem(range, "start");
        cJSON *line = cJSON_GetObjectItem(start, "line");
        cJSON *ch = cJSON_GetObjectItem(start, "character");
        out->line = cJSON_IsNumber(line) ? line->valueint : 0;
        out->col = cJSON_IsNumber(ch) ? ch->valueint : 0;
    }
}

static void handle_response(Editor *e, cJSON *msg) {
    cJSON *method = cJSON_GetObjectItem(msg, "method");
    if (cJSON_IsString(method) && strcmp(method->valuestring, "textDocument/publishDiagnostics") == 0) {
        parse_diagnostics(e, cJSON_GetObjectItem(msg, "params"));
        return;
    }
    cJSON *id = cJSON_GetObjectItem(msg, "id");
    cJSON *result = cJSON_GetObjectItem(msg, "result");
    if (!cJSON_IsNumber(id)) return;
    int n = id->valueint;
    if (!e->lsp.initialized) {
        e->lsp.initialized = true;
        lsp_send_notification(e, "initialized", cJSON_CreateObject());
        return;
    }
    if (n == e->lsp.hover_id) parse_hover(e, result);
    else if (n == e->lsp.completion_id) parse_completion(e, result);
    else if (n == e->lsp.definition_id || n == e->lsp.declaration_id) jump_to_location(e, result);
}

static bool parse_one_message(Editor *e) {
    char *header_end = strstr(e->lsp.read_buf, "\r\n\r\n");
    if (!header_end) return false;
    size_t header_len = (size_t)(header_end - e->lsp.read_buf) + 4;
    char *cl = strstr(e->lsp.read_buf, "Content-Length:");
    if (!cl) return false;
    size_t len = (size_t)strtoul(cl + 15, NULL, 10);
    if (e->lsp.read_len < header_len + len) return false;
    char saved = e->lsp.read_buf[header_len + len];
    e->lsp.read_buf[header_len + len] = 0;
    cJSON *msg = cJSON_Parse(e->lsp.read_buf + header_len);
    e->lsp.read_buf[header_len + len] = saved;
    if (msg) {
        handle_response(e, msg);
        cJSON_Delete(msg);
    }
    memmove(e->lsp.read_buf, e->lsp.read_buf + header_len + len, e->lsp.read_len - header_len - len);
    e->lsp.read_len -= header_len + len;
    e->lsp.read_buf[e->lsp.read_len] = 0;
    return true;
}

void lsp_poll(Editor *e) {
    if (!e->lsp.running || e->lsp.out_fd < 0) return;
    for (;;) {
        if (e->lsp.read_len + 4096 >= sizeof(e->lsp.read_buf)) e->lsp.read_len = 0;
        ssize_t n = read(e->lsp.out_fd, e->lsp.read_buf + e->lsp.read_len, sizeof(e->lsp.read_buf) - e->lsp.read_len - 1);
        if (n > 0) {
            e->lsp.read_len += (size_t)n;
            e->lsp.read_buf[e->lsp.read_len] = 0;
            while (parse_one_message(e)) {}
        } else {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) e->lsp.running = false;
            break;
        }
    }
}

bool lsp_completion_accept(Editor *e) {
    if (!e->lsp.completion_visible || e->lsp.completion_selected >= e->lsp.completion_count) return false;
    const char *s = e->lsp.completions[e->lsp.completion_selected];
    for (size_t i = 0; s[i]; i++) editor_insert_char(e, (unsigned int)(unsigned char)s[i]);
    e->lsp.completion_visible = false;
    return true;
}

void lsp_completion_move(Editor *e, int delta) {
    if (!e->lsp.completion_visible || e->lsp.completion_count == 0) return;
    size_t n = e->lsp.completion_count;
    if (delta > 0) e->lsp.completion_selected = (e->lsp.completion_selected + 1) % n;
    else e->lsp.completion_selected = e->lsp.completion_selected == 0 ? n - 1 : e->lsp.completion_selected - 1;
    const size_t visible_rows = 8;
    if (e->lsp.completion_selected < e->lsp.completion_scroll) {
        e->lsp.completion_scroll = e->lsp.completion_selected;
    } else if (e->lsp.completion_selected >= e->lsp.completion_scroll + visible_rows) {
        e->lsp.completion_scroll = e->lsp.completion_selected + 1 - visible_rows;
    }
    if (e->lsp.completion_scroll + visible_rows > n) {
        e->lsp.completion_scroll = n > visible_rows ? n - visible_rows : 0;
    }
}
