#include "common.h"
#include <unistd.h>

int g_cluster_size = 32;
int g_total_clusters = 32;
int g_disk_host_fat32 = 0;

char current_disk_file[CWD_MAX] = "drive.img";
char g_cwd[CWD_MAX] = ".";

int g_vm_mode = 0;
int g_vm_cleanup = 0;
int g_vm_run_embedded = 0;
char g_vm_root[CWD_MAX] = "";

int shell_running = 1;

int original_stdout_fd = -1;
FILE *original_stdout_file = NULL;

struct termios orig_termios;

int g_history_cleared = 0;
char **g_interactive_history = NULL;
int g_interactive_history_count = 0;
pthread_mutex_t history_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char HELP_BODY[] =
"Usage: BPForbes_Flinstone_Shell [COMMANDS...]\n"
"\n"
"Navigation:\n"
"  cd [path]           Change directory (no args prints current)\n"
"\n"
"Disk operations:\n"
"  createdisk <volume> <rowCount> <nibbleCount> [ -y | -n ]\n"
"       Creates <volume>_disk.img (FAT32) when cluster size is >= 512 B and a multiple of 512;\n"
"       otherwise <volume>_disk.dat (legacy hex lines). Format is detected by content, not extension.\n"
"  format <disk_file> <volume> <rowCount> <nibbleCount>\n"
"       Same geometry rules: large clusters -> FAT32 image at disk_file; small -> legacy hex at disk_file.\n"
"  setdisk <disk_file> Set backing volume (FAT32 image or legacy hex file; any path)\n"
"  listclusters        List disk cluster contents\n"
"  printdisk           Print disk with header\n"
"  writecluster <idx> -t|-h <data>  Write to cluster\n"
"  delcluster <idx>    Zero out cluster\n"
"  update <idx> -t|-h <data>  Delete then write cluster\n"
"  addcluster [ -t|-h <data> ]  Append cluster\n"
"  initdisk <count> <size>  Init in-memory geometry\n"
"  search <text> [ -t |-h ]  Search disk\n"
"  du [ dtl [clusters...] ]  Disk usage\n"
"  import <listfile> <diskfile> [clusters clusterSize]\n"
"  diskput <host_file> [path/on/volume]  Store file under FAT32 root or subdir (parents created)\n"
"  diskget <path/on/volume> <host_file>  Extract file (use diskfiles to see exact names)\n"
"  diskfiles [subdir]   List files (and <DIR> entries) in root or subdir on FAT32 volume\n"
"  diskmkdir <path>     Create nested directories (mkdir -p) on FAT32 volume\n"
"       Path segments must fit FAT 8.3 (short names); use slash or backslash as separators.\n"
"  diskdel <path/on/volume>  Remove file from FAT32 volume (not directories or FLINT.DAT)\n"
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
"  contracts [summary|json] [--help]  Subsystem contract bundle (agents: use `json`)\n"
"  audit [show [N]] | path | ring | --help   Audit log (separate from history; needs FL_AUDIT=1)\n"
"  login <user> <pass>  Switch account (password required)\n"
"  su [-] [user]        Switch user (target user's password)\n"
"  su -c 'cmd' [user]   Run one command as user (target password)\n"
"  sudo [reason]        Grant elevation (your password if not elevated)\n"
"  sudo -i              Root login shell (your password)\n"
"  sudo <command> ...   Run command with jail escape (your password)\n"
"  useradd <u> [p]      Add account (needs elevation; or sudo useradd)\n"
"  userdel <u>          Remove account (needs elevation)\n"
"  passwd [u] <p>       Change password (needs elevation)\n"
"  help                 Display this help\n"
"  redirect <file>      Redirect output (use \"redirect off\" to restore)\n"
"  rerun <N>            Re-run Nth history command\n"
"  exit [ -y | -n ]    Exit shell\n"
"  bios [ -y ]         Reboot into BIOS/UEFI (systemctl reboot --firmware-setup)\n"
"  clear                Clear screen\n"
"  history, his         Show command history\n"
"  cc                   Clear history\n"
"  where [N], loc [N]    Show path log (last N file/dir operations, default 16)\n"
"\n"
"Path support: . = current dir, .. = parent, ./foo = current/foo\n"
"Undoable: create, move, write. Non-undoable: delete, rmtree.\n"
"\n"
"Batch shortcut: ./shell <volume> <rowCount> <nibbleCount> [ -y ]\n"
"  Equivalent to createdisk with same parameters.\n"
"\n"
"Virtualization: ./shell -Virtualization -y [-vm] [commands...]\n"
"  -y: popup (no -vm) or confirm cleanup. -vm: run guest, then this shell. Host files are confined to vm_hostfs/\n"
"  under the directory you started from (or under a temp dir when you pass additional commands). No writes outside that jail.\n"
"\n";

void fl_print_help_message(void) {
    fputs(HELP_BODY, stdout);
    fputs("Author: Bailey Forbes\n", stdout);
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char dbuf[32];
    if (tm && strftime(dbuf, sizeof(dbuf), "%Y-%m-%d", tm) > 0)
        printf("Date:   %s\n", dbuf);
    else
        fputs("Date:   (unknown)\n", stdout);
    putchar('\n');
}
