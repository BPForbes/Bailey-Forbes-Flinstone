#ifndef PATH_LOG_H
#define PATH_LOG_H

#define PATH_LOG_MAX  64
#define PATH_LOG_OP_MAX 16

typedef enum {
    PATH_OP_READ, PATH_OP_WRITE, PATH_OP_CREATE, PATH_OP_DELETE,
    PATH_OP_MOVE, PATH_OP_CD, PATH_OP_DIR
} path_op_t;

void path_log_init(void);
void path_log_record(path_op_t op, const char *path);
void path_log_print(int last_n);
void path_log_shutdown(void);

#endif /* PATH_LOG_H */
