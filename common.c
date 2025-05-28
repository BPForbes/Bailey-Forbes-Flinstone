#include "common.h"

int g_cluster_size = 32;
int g_total_clusters = 32;
char current_disk_file[64] = "drive.txt";

int shell_running = 1;

int original_stdout_fd = -1;
FILE *original_stdout_file = NULL;

struct termios orig_termios;

int g_history_cleared = 0;
char **g_interactive_history = NULL;
int g_interactive_history_count = 0;
pthread_mutex_t history_mutex = PTHREAD_MUTEX_INITIALIZER;

const char *HELP_MSG =
"Usage: new_shell [COMMANDS...]\n"
"\n"
"Primary Commands (each must begin with a dash):\n"
"  -v, -V\n"
"       Print version info and exit. In batch mode, if no -y/-n is provided,\n"
"       -n is assumed. (Use -y to delete history and go interactive.)\n"
"\n"
"  -? | -h | help\n"
"       Display this help message.\n"
"\n"
"  -l\n"
"       List disk contents (all clusters except header).\n"
"\n"
"  -s <searchtext> [ -t|-h ]\n"
"       Search the disk for <searchtext>.\n"
"\n"
"  -w <cluster_index> <-t|-h> <data>\n"
"       Write <data> to the specified cluster.\n"
"\n"
"  -d <cluster_index>\n"
"       Delete (zero out) the specified cluster.\n"
"\n"
"  -up <cluster_index> <-t|-h> <data>\n"
"       Update a cluster: delete then write new data.\n"
"\n"
"  -f <disk_file>\n"
"       Set the disk file to use.\n"
"\n"
"  -cd <volume> <rowCount> <nibbleCount> [<interactive>]\n"
"       Create a new disk file (<volume>_disk.txt) with random contents,\n"
"       then print its contents. The optional interactive parameter is -y or -n.\n"
"       In batch mode if omitted, -n is assumed.\n"
"\n"
"  -F <disk_file> <volume> <rowCount> <nibbleCount>\n"
"       Format an existing disk file with random contents.\n"
"\n"
"  (Batch shortcut:) new_shell <volume> <rowCount> <nibbleCount> [<interactive>]\n"
"       Equivalent to -cd with the same parameters.\n"
"\n"
"  dir [path]\n"
"       List files in the specified directory (or current directory if omitted).\n"
"\n"
"  -D <directory>\n"
"       Create a new directory.\n"
"\n"
" -L\n"
"       List local directories.\n"
"\n"
"  -R <directory>\n"
"       Recursively remove the specified directory.\n"
"\n"
"  -du [ -dtl <cluster_numbers>... ]\n"
"       Show disk metadata; with -dtl, display detailed info for specified clusters.\n"
"\n"
"  -O <filename>\n"
"       Redirect output to the specified file; use \"-O off\" to restore.\n"
"\n"
"  -init <cluster_count> <cluster_size>\n"
"       Soft initialize the in‑memory disk geometry (without altering the disk file).\n"
"\n"
"  -uc <N>\n"
"       Re-run the Nth command from history.\n"
"\n"
"  -import <textfile> <txtfile> [clusters clusterSize]\n"
"       Import a textual drive listing into a disk file.\n"
"\n"
"  -print\n"
"       Print the disk with header and cluster addressing.\n"
"\n"
"Word-based commands (without a dash):\n"
"  make <filename>    (create file; end with line \"EOF\")\n"
"  exit [ -y | -n ]   Exit shell (in batch mode, -n is assumed if not provided)\n"
"  clear              Clear the screen\n"
"  history | his      Display command history\n"
"  cc                 Clear command history\n"
"  cat <filename>     Display file contents.\n"
"\n"
"Author: Bailey Forbes\n"
"Date:   03/07/25\n";
