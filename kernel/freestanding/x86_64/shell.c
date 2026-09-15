#include <stdint.h>
#include "shell.h"
#include "commands.h"
#include "identity.h"
#include "keyboard.h"
#include "serial.h"
#include "vga.h"

#define LINE 128
#define HIST_MAX 16
#define MODE_CMD 0
#define MODE_LOGIN 1
#define MODE_SU 2
#define MODE_USERADD 3
#define MODE_SUDO 4
#define MODE_SUDO_CMD 5
#define MODE_SUDO_I 6
#define MODE_PASSWD 7

struct perspective {
    int valid;
    uint16_t cells[FL_FS_VGA_SHELL_CELLS];
    int row;
    int col;
    char line[LINE];
    unsigned len;
    int mode;
    char pending[LINE];
    char history[HIST_MAX][LINE];
    unsigned history_count;
};

static char s_line[FL_FS_MAX_SESSIONS][LINE];
static unsigned s_len[FL_FS_MAX_SESSIONS];
static int s_mode[FL_FS_MAX_SESSIONS];
static char s_pending[FL_FS_MAX_SESSIONS][LINE];
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
#ifdef FL_WASM_SHELL_PROMPT
    emit("shell> ");
#else
    emit(fl_fs_identity_user(session));
    emit("@flintstone> ");
#endif
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

static void history_clear(int user_idx)
{
    if (user_idx < 0 || user_idx >= FL_FS_MAX_USERS)
        return;
    s_perspectives[user_idx].history_count = 0;
}

static int history_nth(int user_idx, unsigned n, char *out, unsigned cap)
{
    const struct perspective *view;
    if (user_idx < 0 || user_idx >= FL_FS_MAX_USERS || !out || cap == 0 || n == 0)
        return 0;
    view = &s_perspectives[user_idx];
    if (n > view->history_count)
        return 0;
    str_copy(out, view->history[n - 1], cap);
    return 1;
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

static void run_command(int session, char *line);

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
    else if (mode == MODE_SUDO || mode == MODE_SUDO_CMD || mode == MODE_SUDO_I)
        ok = fl_fs_identity_sudo(session, password);
    else if (mode == MODE_PASSWD)
        ok = fl_fs_identity_passwd(session, s_pending[session], password);
    if (ok && mode == MODE_SUDO_I)
        ok = fl_fs_identity_switchuser(session, "root");
    emit(ok ? "ok\r\n" : "authentication failed\r\n");
    if (ok && fl_fs_identity_user_index(session) != before)
        perspective_switch_user(fl_fs_identity_user_index(session), session);
    if (ok && mode == MODE_SUDO_CMD && s_pending[session][0]) {
        char held[LINE];
        str_copy(held, s_pending[session], sizeof(held));
        s_pending[session][0] = 0;
        run_command(session, held);
        return;
    }
    announce_session();
}

static void run_command(int session, char *line)
{
    char verb[16];
    const char *cursor = line;
    int user_idx = active_user_index();
    static int rerunning;
    if (!take_word(&cursor, verb, sizeof(verb)))
        return;
    if (user_idx >= 0 && line[0])
        history_append(user_idx, line);
    if (str_eq(verb, "history") || str_eq(verb, "his")) {
        history_show(user_idx);
        return;
    }
    if (str_eq(verb, "cc")) {
        history_clear(user_idx);
        emit("history cleared\r\n");
        return;
    }
    if (str_eq(verb, "rerun")) {
        char arg[8];
        char held[LINE];
        unsigned n;
        if (!take_word(&cursor, arg, sizeof(arg))) {
            emit("usage: rerun <N>\r\n");
            return;
        }
        n = 0;
        {
            const char *p = arg;
            while (*p >= '0' && *p <= '9')
                n = n * 10u + (unsigned)(*p++ - '0');
        }
        if (rerunning) {
            emit("rerun nested\r\n");
            return;
        }
        if (!history_nth(user_idx, n, held, sizeof(held))) {
            emit("no such history entry\r\n");
            return;
        }
        rerunning = 1;
        run_command(session, held);
        rerunning = 0;
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
    if (str_eq(verb, "sudo")) {
        char arg[LINE];
        if (!take_word(&cursor, arg, sizeof(arg))) {
            s_mode[session] = MODE_SUDO;
            return;
        }
        if (str_eq(arg, "-k")) {
            fl_fs_identity_sudo_k(session);
            emit("sudo revoked\r\n");
            return;
        }
        if (str_eq(arg, "-i")) {
            s_mode[session] = MODE_SUDO_I;
            return;
        }
        skip_spaces(&cursor);
        {
            unsigned n = 0;
            while (arg[n] && n + 1 < LINE) {
                s_pending[session][n] = arg[n];
                ++n;
            }
            if (*cursor && n + 1 < LINE)
                s_pending[session][n++] = ' ';
            while (*cursor && n + 1 < LINE)
                s_pending[session][n++] = *cursor++;
            s_pending[session][n] = 0;
        }
        s_mode[session] = MODE_SUDO_CMD;
        return;
    }
    if (str_eq(verb, "passwd")) {
        char name[16];
        if (!take_word(&cursor, name, sizeof(name)))
            str_copy(name, fl_fs_identity_user(session), sizeof(name));
        str_copy(s_pending[session], name, sizeof(s_pending[session]));
        s_mode[session] = MODE_PASSWD;
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
            /* A brand-new slot must not inherit the previous session's
             * just-executed command line (e.g. "session new" + "switchuser"). */
            s_len[created] = 0;
            s_line[created][0] = 0;
            s_mode[created] = MODE_CMD;
            s_pending[created][0] = 0;
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
    if (fl_fs_commands_run(session, verb, &cursor))
        return;
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
    fl_fs_commands_init();
    emit("lab shell: identity, ramfs, labdisk, labnet, server relay\r\n");
    announce_session();
    prompt();
}

void fl_fs_shell_input(char c)
{
    int session = active_session();
    if (c == '\r')
        c = '\n';
    if (c == '\n') {
        char line[LINE];
        emit("\r\n");
        s_line[session][s_len[session]] = 0;
        str_copy(line, s_line[session], LINE);
        /* Clear before dispatch so session new / switchuser cannot snapshot
         * the completed command into the next session's line buffer. */
        s_len[session] = 0;
        s_line[session][0] = 0;
        if (s_mode[session] != MODE_CMD)
            finish_password(session, line);
        else
            run_command(session, line);
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
#if defined(__EMSCRIPTEN__) || defined(FL_WASM_SHELL_PROMPT)
        while (fl_fs_kbd_getc(&c))
            fl_fs_shell_input(c);
        while (fl_fs_serial_getc(&c))
            fl_fs_shell_input(c);
        return;
#else
        __asm__ volatile("hlt");
        while (fl_fs_kbd_getc(&c))
            fl_fs_shell_input(c);
        while (fl_fs_serial_getc(&c))
            fl_fs_shell_input(c);
#endif
    }
}
