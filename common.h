#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <termios.h>
#include <time.h>

#define VERSION                "0.02"
#define HISTORY_FILE           "shell_history.txt"
#define NUM_WORKERS            4
#define MAX_JOBS               64

/* Global disk parameters */
extern int g_cluster_size;
extern int g_total_clusters;
extern char current_disk_file[64];

/* Shell state */
extern int shell_running;

/* Original stdout */
extern int original_stdout_fd;
extern FILE *original_stdout_file;

/* Terminal settings */
extern struct termios orig_termios;

/* History and interactive mode globals */
extern int g_history_cleared;
extern char **g_interactive_history;
extern int g_interactive_history_count;
extern pthread_mutex_t history_mutex;

/* Help message */
extern const char *HELP_MSG;

#endif /* COMMON_H */
