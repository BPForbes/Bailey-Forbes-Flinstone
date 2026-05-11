#ifndef COMMON_H
#define COMMON_H

#include "version_def.h"

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

/* Legacy hex-disk volumes: host-side history file. FAT32 images use HISTORY_DISK_PATH on the volume. */
#define HISTORY_FILE           "shell_history.txt"
#define HISTORY_DISK_PATH      "SH_HIST.TXT"
#define NUM_WORKERS            4
#define MAX_JOBS               64
#define CWD_MAX                256

/* Global disk parameters */
extern int g_cluster_size;
extern int g_total_clusters;
extern char current_disk_file[CWD_MAX];
/* 1 = FAT32 super-floppy .img (FLINT.DAT payload); 0 = legacy hex .txt */
extern int g_disk_host_fat32;

/* VM mode: 1 = -Virtualization; file access is jailed under launch dir or sandbox in vm_hostfs/ (see fs_jail.c) */
extern int g_vm_mode;
extern int g_vm_cleanup;   /* 1 = delete temp dir on exit (-y) */
extern int g_vm_run_embedded;  /* 1 = run embedded x86 VM instead of shell */
extern char g_vm_root[CWD_MAX];

/* Current working directory */
extern char g_cwd[CWD_MAX];

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

/* Help text (body + runtime date footer via fl_print_help_message) */
void fl_print_help_message(void);

#endif /* COMMON_H */
