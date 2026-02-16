#include "path_log.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    path_op_t op;
    char path[256];
} path_log_entry_t;

static path_log_entry_t s_log[PATH_LOG_MAX];
static int s_head;
static int s_count;
static int s_inited;

static const char *op_str(path_op_t op) {
    switch (op) {
        case PATH_OP_READ:   return "read";
        case PATH_OP_WRITE:  return "write";
        case PATH_OP_CREATE: return "create";
        case PATH_OP_DELETE: return "delete";
        case PATH_OP_MOVE:   return "move";
        case PATH_OP_CD:     return "cd";
        case PATH_OP_DIR:    return "dir";
        default:             return "?";
    }
}

void path_log_init(void) {
    if (s_inited) return;
    memset(s_log, 0, sizeof(s_log));
    s_head = s_count = 0;
    s_inited = 1;
}

void path_log_record(path_op_t op, const char *path) {
    if (!s_inited || !path) return;
    s_log[s_head].op = op;
    strncpy(s_log[s_head].path, path, sizeof(s_log[s_head].path) - 1);
    s_log[s_head].path[255] = '\0';
    s_head = (s_head + 1) % PATH_LOG_MAX;
    if (s_count < PATH_LOG_MAX) s_count++;
}

void path_log_print(int last_n) {
    if (!s_inited) return;
    if (last_n <= 0 || last_n > PATH_LOG_MAX) last_n = s_count;
    int start = s_count < PATH_LOG_MAX ? 0 : (s_head + PATH_LOG_MAX - last_n) % PATH_LOG_MAX;
    int total = s_count < PATH_LOG_MAX ? s_count : PATH_LOG_MAX;
    printf("Path log (last %d operations):\n", last_n > total ? total : last_n);
    for (int i = 0; i < last_n && i < total; i++) {
        int idx = (start + i) % PATH_LOG_MAX;
        printf("  [%s] %s\n", op_str(s_log[idx].op), s_log[idx].path);
    }
}

void path_log_shutdown(void) {
    s_inited = 0;
}
