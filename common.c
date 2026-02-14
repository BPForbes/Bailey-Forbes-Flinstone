#include "common.h"
#include <unistd.h>

int g_cluster_size = 32;
int g_total_clusters = 32;
char current_disk_file[64] = "drive.txt";
char g_cwd[CWD_MAX] = ".";

int shell_running = 1;

int original_stdout_fd = -1;
FILE *original_stdout_file = NULL;

struct termios orig_termios;

int g_history_cleared = 0;
char **g_interactive_history = NULL;
int g_interactive_history_count = 0;
pthread_mutex_t history_mutex = PTHREAD_MUTEX_INITIALIZER;

const char *HELP_MSG =
"Usage: BPForbes_Flinstone_Shell [COMMANDS...]\n"
"\n"
"Navigation:\n"
"  cd [path]           Change directory (no args prints current)\n"
"\n"
"Disk operations:\n"
"  createdisk <volume> <rowCount> <nibbleCount> [ -y | -n ]\n"
"       Create new disk file (<volume>_disk.txt)\n"
"  format <disk_file> <volume> <rowCount> <nibbleCount>\n"
"       Format existing disk file\n"
"  setdisk <disk_file> Set disk file to use\n"
"  listclusters        List disk cluster contents\n"
"  printdisk           Print disk with header\n"
"  writecluster <idx> -t|-h <data>  Write to cluster\n"
"  delcluster <idx>    Zero out cluster\n"
"  update <idx> -t|-h <data>  Delete then write cluster\n"
"  addcluster [ -t|-h <data> ]  Append cluster\n"
"  initdisk <count> <size>  Init in-memory geometry\n"
"  search <text> [ -t |-h ]  Search disk\n"
"  du [ dtl [clusters...] ]  Disk usage\n"
"  import <textfile> <txtfile> [clusters clusterSize]\n"
"\n"
"Directory operations:\n"
"  dir [path]          List directory contents\n"
"  listdirs            List directories in cwd\n"
"  mkdir <path>        Create directory (parents created if needed)\n"
"  rmdir <path>        Remove empty directory\n"
"  rmtree <path>       Recursively remove directory\n"
"\n"
"File operations:\n"
"  make <filename>     Create file (interactive, end with EOF)\n"
"  write <file> <content>  Write content to file\n"
"  cat <filename>      Display file contents\n"
"  type <filename>     Same as cat\n"
"  mv <src> <dst>      Move or rename file/directory\n"
"\n"
"Other:\n"
"  version [ -y | -n ]  Print version\n"
"  help                 Display this help\n"
"  redirect <file>      Redirect output (use \"redirect off\" to restore)\n"
"  rerun <N>            Re-run Nth history command\n"
"  exit [ -y | -n ]    Exit shell\n"
"  clear                Clear screen\n"
"  history, his         Show command history\n"
"  cc                   Clear history\n"
"  where [N], loc [N]    Show path log (last N file/dir operations, default 16)\n"
"\n"
"Path support: . = current dir, .. = parent, ./foo = current/foo\n"
"\n"
"Batch shortcut: ./shell <volume> <rowCount> <nibbleCount> [ -y ]\n"
"  Equivalent to createdisk with same parameters.\n"
"\n"
"Author: Bailey Forbes\n"
"Date:   03/07/25\n";
