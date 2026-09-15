#include <stdint.h>
#include "shell.h"
#include "identity.h"
#include "keyboard.h"
#include "ramfs.h"
#include "serial.h"
#include "vga.h"

#define LINE 128
#define HIST_MAX 16
#define MODE_CMD 0
#define MODE_LOGIN 1
#define MODE_SU 2
#define MODE_USERADD 3

struct perspective {
    int valid;
    uint16_t cells[FL_FS_VGA_SHELL_CELLS];
    int row;
    int col;
    char line[LINE];
    unsigned len;
    int mode;
    char pending[16];
    char history[HIST_MAX][LINE];
    unsigned history_count;
};

static char s_line[FL_FS_MAX_SESSIONS][LINE];
static unsigned s_len[FL_FS_MAX_SESSIONS];
static int s_mode[FL_FS_MAX_SESSIONS];
static char s_pending[FL_FS_MAX_SESSIONS][16];
static struct perspective s_perspectives[FL_FS_MAX_USERS];

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, unsigned cap)
{
    unsigned i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void emit_char(char c)
{
    fl_fs_serial_putc(c);
    fl_fs_vga_putc(c);
}

static void emit(const char *s)
{
    while (*s)
        emit_char(*s++);
}

static void emit_uint(unsigned value)
{
    char buf[10];
    int n = 0;
    if (value == 0) {
        emit_char('0');
        return;
    }
    while (value && n < 10) {
        buf[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n--)
        emit_char(buf[n]);
}

static int active_session(void)
{
    return fl_fs_session_active();
}

static int active_user_index(void)
{
    return fl_fs_identity_user_index(active_session());
}

static void refresh_status(void)
{
    int session = active_session();
    fl_fs_vga_status(fl_fs_identity_user(session), (unsigned)session + 1,
                     (unsigned)fl_fs_session_count());
}

static void announce_session(void)
{
    int session = active_session();
    emit("SESSION ");
    emit_uint((unsigned)session + 1);
    emit(" user=");
    emit(fl_fs_identity_user(session));
    emit("\r\n");
    refresh_status();
}

static void announce_switchuser(void)
{
    int session = active_session();
    emit("SWITCHUSER user=");
    emit(fl_fs_identity_user(session));
    emit("\r\n");
    refresh_status();
}

static void prompt(void)
{
    int session = active_session();
    if (s_mode[session] != MODE_CMD) {
        emit("Password: ");
        return;
    }
    emit(fl_fs_identity_user(session));
    emit("@flintstone> ");
}

static void skip_spaces(const char **cursor)
{
    while (**cursor == ' ')
        ++*cursor;
}

static int take_word(const char **cursor, char *out, unsigned cap)
{
    unsigned n = 0;
    skip_spaces(cursor);
    if (**cursor == 0)
        return 0;
    while (**cursor && **cursor != ' ' && n + 1 < cap) {
        out[n++] = *(*cursor)++;
    }
    out[n] = 0;
    return 1;
}

static void visit_user(const char *name, int elevated, void *ctx)
{
    (void)ctx;
    emit(name);
    emit(elevated ? " elevated\r\n" : "\r\n");
}

static void history_append(int user_idx, const char *line)
{
    struct perspective *view;
    unsigned slot;
    if (user_idx < 0 || user_idx >= FL_FS_MAX_USERS || !line || !line[0])
        return;
    view = &s_perspectives[user_idx];
    if (view->history_count >= HIST_MAX) {
        for (unsigned i = 1; i < HIST_MAX; ++i)
            str_copy(view->history[i - 1], view->history[i], LINE);
        slot = HIST_MAX - 1;
    } else {
        slot = view->history_count++;
    }
    str_copy(view->history[slot], line, LINE);
}

static void history_show(int user_idx)
{
    const struct perspective *view;
    if (user_idx < 0 || user_idx >= FL_FS_MAX_USERS)
        return;
    view = &s_perspectives[user_idx];
    if (view->history_count == 0) {
        emit("history empty\r\n");
        return;
    }
    for (unsigned i = 0; i < view->history_count; ++i) {
        emit_uint(i + 1);
        emit(": ");
        emit(view->history[i]);
        emit("\r\n");
    }
}

static void perspective_save(int user_idx, int session)
{
    struct perspective *view;
    if (user_idx < 0 || user_idx >= FL_FS_MAX_USERS || session < 0 || session >= FL_FS_MAX_SESSIONS)
        return;
    view = &s_perspectives[user_idx];
    view->valid = 1;
    fl_fs_vga_snapshot_shell(view->cells, FL_FS_VGA_SHELL_CELLS);
    fl_fs_vga_get_cursor(&view->row, &view->col);
    str_copy(view->line, s_line[session], LINE);
    view->len = s_len[session];
    view->mode = s_mode[session];
    str_copy(view->pending, s_pending[session], sizeof(view->pending));
}

static void perspective_load(int user_idx, int session)
{
    struct perspective *view;
    if (user_idx < 0 || user_idx >= FL_FS_MAX_USERS || session < 0 || session >= FL_FS_MAX_SESSIONS)
        return;
    view = &s_perspectives[user_idx];
    if (!view->valid) {
        fl_fs_vga_clear_shell();
        s_len[session] = 0;
        s_mode[session] = MODE_CMD;
        s_pending[session][0] = 0;
        s_line[session][0] = 0;
        return;
    }
    fl_fs_vga_restore_shell(view->cells, FL_FS_VGA_SHELL_CELLS);
    fl_fs_vga_set_cursor(view->row, view->col);
    str_copy(s_line[session], view->line, LINE);
    s_len[session] = view->len;
    s_mode[session] = view->mode;
    str_copy(s_pending[session], view->pending, sizeof(s_pending[session]));
}

static void perspective_switch_user(int user_idx, int session)
{
    int current = active_user_index();
    if (current >= 0)
        perspective_save(current, session);
    perspective_load(user_idx, session);
}

static void perspective_save_session(int session)
{
    int user_idx = fl_fs_identity_user_index(session);
    if (user_idx >= 0)
        perspective_save(user_idx, session);
}

static void perspective_load_session(int session)
{
    int user_idx = fl_fs_identity_user_index(session);
    if (user_idx >= 0)
        perspective_load(user_idx, session);
}

static void session_go(int session)
{
    perspective_load_session(session);
}

static int switchuser_to(int session, const char *name)
{
    int target;
    if (!fl_fs_identity_switchuser(session, name))
        return 0;
    target = fl_fs_identity_user_index(session);
    perspective_switch_user(target, session);
    announce_switchuser();
    return 1;
}

static void fs_status(int rc)
{
    if (rc == 0)
        return;
    if (rc == -1)
        emit("not found\r\n");
    else if (rc == -2)
        emit("not a directory\r\n");
    else if (rc == -3)
        emit("already exists\r\n");
    else if (rc == -4)
        emit("ramfs full\r\n");
    else if (rc == -5)
        emit("not a file\r\n");
    else if (rc == -7)
        emit("directory not empty\r\n");
    else
        emit("bad path\r\n");
}

static void dir_visit(const char *name, int is_dir, unsigned size, void *ctx)
{
    (void)ctx;
    emit(is_dir ? "d " : "f ");
    emit(name);
    if (!is_dir) {
        emit(" ");
        emit_uint(size);
    }
    emit("\r\n");
}

static int run_fs(int session, const char *verb, const char **cursor)
{
    char path[FL_FS_RAMFS_PATH];
    if (str_eq(verb, "pwd")) {
        fl_fs_ramfs_pwd(session, path, sizeof(path));
        emit(path);
        emit("\r\n");
        return 1;
    }
    if (str_eq(verb, "cd")) {
        if (!take_word(cursor, path, sizeof(path)))
            str_copy(path, "/", sizeof(path));
        fs_status(fl_fs_ramfs_cd(session, path));
        return 1;
    }
    if (str_eq(verb, "dir") || str_eq(verb, "ls")) {
        int rc;
        if (!take_word(cursor, path, sizeof(path)))
            path[0] = 0;
        rc = fl_fs_ramfs_dir(session, path, dir_visit, 0);
        if (rc == 0 && !path[0])
            return 1;
        fs_status(rc);
        return 1;
    }
    if (str_eq(verb, "mkdir")) {
        if (!take_word(cursor, path, sizeof(path))) {
            emit("usage: mkdir <dir>\r\n");
            return 1;
        }
        fs_status(fl_fs_ramfs_mkdir(session, path));
        return 1;
    }
    if (str_eq(verb, "rm")) {
        if (!take_word(cursor, path, sizeof(path))) {
            emit("usage: rm <path>\r\n");
            return 1;
        }
        fs_status(fl_fs_ramfs_rm(session, path));
        return 1;
    }
    if (str_eq(verb, "cat")) {
        char data[FL_FS_RAMFS_DATA];
        int rc;
        if (!take_word(cursor, path, sizeof(path))) {
            emit("usage: cat <file>\r\n");
            return 1;
        }
        rc = fl_fs_ramfs_cat(session, path, data, sizeof(data));
        if (rc != 0) {
            fs_status(rc);
            return 1;
        }
        emit(data);
        emit("\r\n");
        return 1;
    }
    if (str_eq(verb, "write")) {
        if (!take_word(cursor, path, sizeof(path))) {
            emit("usage: write <file> <text>\r\n");
            return 1;
        }
        skip_spaces(cursor);
        if (!**cursor) {
            emit("usage: write <file> <text>\r\n");
            return 1;
        }
        {
            int rc = fl_fs_ramfs_write(session, path, *cursor);
            if (rc == 0) {
                emit("wrote ");
                emit(path);
                emit("\r\n");
            } else {
                fs_status(rc);
            }
        }
        return 1;
    }
    return 0;
}

static int run_server(const char *verb, const char **cursor)
{
    char sub[16];
    if (!str_eq(verb, "server"))
        return 0;
    if (!take_word(cursor, sub, sizeof(sub))) {
        emit("server host|join|leave|msg <text>\r\n");
        emit("browser relay (same P3 session wire as net_server.c)\r\n");
        return 1;
    }
    if (str_eq(sub, "host") || str_eq(sub, "join") || str_eq(sub, "leave")) {
        emit("SERVER_RELAY ");
        emit(sub);
        emit("\r\n");
        return 1;
    }
    if (str_eq(sub, "msg")) {
        skip_spaces(cursor);
        if (!**cursor) {
            emit("usage: server msg <text>\r\n");
            return 1;
        }
        emit("SERVER_RELAY msg ");
        emit(*cursor);
        emit("\r\n");
        return 1;
    }
    emit("unknown server verb; try server\r\n");
    return 1;
}

static void finish_password(int session, const char *password)
{
    int ok = 0;
    int mode = s_mode[session];
    int before = fl_fs_identity_user_index(session);
    s_mode[session] = MODE_CMD;
    if (mode == MODE_LOGIN)
        ok = fl_fs_identity_login(session, s_pending[session], password);
    else if (mode == MODE_SU)
        ok = fl_fs_identity_su(session, s_pending[session], password);
    else if (mode == MODE_USERADD)
        ok = fl_fs_identity_useradd(session, s_pending[session], password);
    emit(ok ? "ok\r\n" : "authentication failed\r\n");
    if (ok && fl_fs_identity_user_index(session) != before)
        perspective_switch_user(fl_fs_identity_user_index(session), session);
    announce_session();
}

static void run_command(int session, char *line)
{
    char verb[16];
    const char *cursor = line;
    int user_idx = active_user_index();
    if (!take_word(&cursor, verb, sizeof(verb)))
        return;
    if (user_idx >= 0 && line[0])
        history_append(user_idx, line);
    if (str_eq(verb, "help")) {
        emit("help whoami users history switchuser login su logout useradd session\r\n");
        emit("dir ls cat write mkdir rm pwd cd  (lab ramfs)\r\n");
        emit("server host|join|leave|msg  (browser relay)\r\n");
        emit("hosted FAT32 and kernel/core/net server stay on the ELF shell\r\n");
        return;
    }
    if (run_fs(session, verb, &cursor) || run_server(verb, &cursor))
        return;
    if (str_eq(verb, "whoami")) {
        emit("WHOAMI ");
        emit(fl_fs_identity_user(session));
        emit(fl_fs_identity_elevated(session) ? " elevated\r\n" : "\r\n");
        return;
    }
    if (str_eq(verb, "history")) {
        history_show(user_idx);
        return;
    }
    if (str_eq(verb, "users")) {
        fl_fs_identity_each_user(visit_user, 0);
        return;
    }
    if (str_eq(verb, "logout")) {
        int before = fl_fs_identity_user_index(session);
        fl_fs_identity_logout(session);
        if (fl_fs_identity_user_index(session) != before)
            perspective_switch_user(fl_fs_identity_user_index(session), session);
        announce_session();
        return;
    }
    if (str_eq(verb, "switchuser")) {
        char name[16];
        if (!take_word(&cursor, name, sizeof(name))) {
            emit("usage: switchuser <user>\r\n");
            return;
        }
        if (!switchuser_to(session, name))
            emit("unknown user\r\n");
        return;
    }
    if (str_eq(verb, "login") || str_eq(verb, "su") || str_eq(verb, "useradd")) {
        char name[16];
        if (!take_word(&cursor, name, sizeof(name))) {
            if (str_eq(verb, "su"))
                str_copy(name, "root", sizeof(name));
            else {
                emit("usage: ");
                emit(verb);
                emit(" <user>\r\n");
                return;
            }
        }
        str_copy(s_pending[session], name, sizeof(s_pending[session]));
        s_mode[session] = str_eq(verb, "login") ? MODE_LOGIN : (str_eq(verb, "su") ? MODE_SU : MODE_USERADD);
        return;
    }
    if (str_eq(verb, "session")) {
        char arg[16];
        if (!take_word(&cursor, arg, sizeof(arg))) {
            emit("sessions ");
            emit_uint((unsigned)fl_fs_session_count());
            emit(" active ");
            emit_uint((unsigned)fl_fs_session_active() + 1);
            emit("\r\n");
            return;
        }
        if (str_eq(arg, "new")) {
            int current = active_session();
            int created = fl_fs_session_new();
            if (created < 0) {
                emit("session table full\r\n");
                return;
            }
            if (current >= 0 && current != created)
                perspective_save_session(current);
            session_go(created);
            announce_session();
            return;
        }
        unsigned id = 0;
        const char *p = arg;
        while (*p >= '0' && *p <= '9')
            id = id * 10u + (unsigned)(*p++ - '0');
        if (id == 0) {
            emit("no such session\r\n");
            return;
        }
        {
            int current = active_session();
            int target = (int)id - 1;
            if (current >= 0 && current != target)
                perspective_save_session(current);
            if (!fl_fs_session_switch(target)) {
                emit("no such session\r\n");
                return;
            }
            session_go(target);
            announce_session();
        }
        return;
    }
    emit("unknown command; try help\r\n");
}

void fl_fs_shell_init(void)
{
    for (int i = 0; i < FL_FS_MAX_SESSIONS; ++i) {
        s_len[i] = 0;
        s_mode[i] = MODE_CMD;
        s_pending[i][0] = 0;
        s_line[i][0] = 0;
    }
    for (int i = 0; i < FL_FS_MAX_USERS; ++i)
        s_perspectives[i].valid = 0;
    emit("lab shell: identity, ramfs, server relay\r\n");
    announce_session();
    prompt();
}

void fl_fs_shell_input(char c)
{
    int session = active_session();
    if (c == '\r')
        c = '\n';
    if (c == '\n') {
        emit("\r\n");
        s_line[session][s_len[session]] = 0;
        if (s_mode[session] != MODE_CMD)
            finish_password(session, s_line[session]);
        else
            run_command(session, s_line[session]);
        s_len[session] = 0;
        prompt();
        return;
    }
    if (c == '\b' || c == 127) {
        if (s_len[session] == 0)
            return;
        --s_len[session];
        if (s_mode[session] == MODE_CMD)
            emit("\b \b");
        return;
    }
    if (c < 32 || c > 126)
        return;
    if (s_len[session] + 1 >= LINE)
        return;
    s_line[session][s_len[session]++] = c;
    emit_char(s_mode[session] == MODE_CMD ? c : '*');
}

void fl_fs_shell_run(void)
{
    char c;
    for (;;) {
        __asm__ volatile("hlt");
        while (fl_fs_kbd_getc(&c))
            fl_fs_shell_input(c);
        while (fl_fs_serial_getc(&c))
            fl_fs_shell_input(c);
    }
}
