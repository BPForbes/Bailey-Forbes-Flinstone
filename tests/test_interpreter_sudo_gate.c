/* Every builtin passes the sudo window's signing gate once (cmd_sudo_sign_elevated, wrapped with -Wl,--wrap). */
/* A sudo signs through cmd_sudo_sign_action; a blank line signs nothing. */
#include "interpreter.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int s_calls;
static char s_last[512];

void __wrap_cmd_sudo_sign_elevated(const char *line);
void __wrap_cmd_sudo_sign_elevated(const char *line) {
    s_calls++;
    snprintf(s_last, sizeof s_last, "%s", line ? line : "");
}

static int s_failed;

static void expect_gate(const char *line, int calls) {
    s_calls = 0;
    s_last[0] = '\0';
    (void)execute_command_str(line);
    if (s_calls != calls || (calls == 1 && strcmp(s_last, line) != 0)) {
        fprintf(stderr, "FAIL: \"%s\": gate called %d time(s) (want %d), last \"%s\"\n", line, s_calls, calls,
                s_last);
        s_failed = 1;
    } else {
        printf("  ok: \"%s\" -> gate called %d time(s)\n", line, calls);
    }
}

int main(void) {
    char dir[] = "/tmp/fl_interp_gate_XXXXXX";
    char path[600];
    DIR *d;
    struct dirent *e;

    if (!mkdtemp(dir) || chdir(dir) != 0) {
        perror("test_interpreter_sudo_gate: temp dir");
        return 1;
    }
    if (!freopen("/dev/null", "r", stdin)) {
        perror("test_interpreter_sudo_gate: stdin");
        return 1;
    }

    expect_gate("help", 1);
    expect_gate("history", 1);
    expect_gate("make gate_made.txt", 1);
    expect_gate("clear", 1);
    expect_gate("version -n", 1);
    expect_gate("sudo -k", 0);
    expect_gate("\"sudo\" -k", 0);
    expect_gate("   ", 0);

    d = opendir(dir);
    while (d && (e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        (void)unlink(path);
    }
    if (d)
        closedir(d);
    (void)chdir("/");
    (void)rmdir(dir);
    if (s_failed)
        return 1;
    printf("test_interpreter_sudo_gate: OK\n");
    return 0;
}
