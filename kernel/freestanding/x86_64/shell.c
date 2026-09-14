#include "shell.h"
#include "identity.h"
#include "keyboard.h"
#include "serial.h"
#include "vga.h"

#define LINE 96
#define MODE_CMD 0
#define MODE_LOGIN 1
#define MODE_SU 2
#define MODE_USERADD 3

static char s_line[FL_FS_MAX_SESSIONS][LINE];
static unsigned s_len[FL_FS_MAX_SESSIONS];
static int s_mode[FL_FS_MAX_SESSIONS];
static char s_pending[FL_FS_MAX_SESSIONS][16];

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

static void refresh_status(void)
{
    int session = fl_fs_session_active();
    fl_fs_vga_status(fl_fs_identity_user(session), (unsigned)session + 1,
                     (unsigned)fl_fs_session_count());
}

static void announce_session(void)
{
    int session = fl_fs_session_active();
    emit("SESSION ");
    emit_uint((unsigned)session + 1);
    emit(" user=");
    emit(fl_fs_identity_user(session));
    emit("\r\n");
    refresh_status();
}

static void prompt(void)
{
    int session = fl_fs_session_active();
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

static void finish_password(int session, const char *password)
{
    int ok = 0;
    int mode = s_mode[session];
    s_mode[session] = MODE_CMD;
    if (mode == MODE_LOGIN)
        ok = fl_fs_identity_login(session, s_pending[session], password);
    else if (mode == MODE_SU)
        ok = fl_fs_identity_su(session, s_pending[session], password);
    else if (mode == MODE_USERADD)
        ok = fl_fs_identity_useradd(session, s_pending[session], password);
    emit(ok ? "ok\r\n" : "authentication failed\r\n");
    announce_session();
}

static void run_command(int session, char *line)
{
    char verb[16];
    const char *cursor = line;
    if (!take_word(&cursor, verb, sizeof(verb)))
        return;
    if (str_eq(verb, "help")) {
        emit("help whoami users login su logout useradd session\r\n");
        emit("filesystem, network, and server remain hosted-only\r\n");
        return;
    }
    if (str_eq(verb, "whoami")) {
        emit("WHOAMI ");
        emit(fl_fs_identity_user(session));
        emit(fl_fs_identity_elevated(session) ? " elevated\r\n" : "\r\n");
        return;
    }
    if (str_eq(verb, "users")) {
        fl_fs_identity_each_user(visit_user, 0);
        return;
    }
    if (str_eq(verb, "logout")) {
        fl_fs_identity_logout(session);
        announce_session();
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
            int created = fl_fs_session_new();
            if (created < 0) {
                emit("session table full\r\n");
                return;
            }
            announce_session();
            return;
        }
        unsigned id = 0;
        const char *p = arg;
        while (*p >= '0' && *p <= '9')
            id = id * 10u + (unsigned)(*p++ - '0');
        if (id == 0 || !fl_fs_session_switch((int)id - 1)) {
            emit("no such session\r\n");
            return;
        }
        announce_session();
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
    emit("lab identity: login/su/logout/whoami/session (lab seeds flinstone/root)\r\n");
    announce_session();
    prompt();
}

void fl_fs_shell_input(char c)
{
    int session = fl_fs_session_active();
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
