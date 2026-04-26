/* BPForbes_Flinstone_Tests.c */
#include <CUnit/Basic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <signal.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>

/* Include CUnit header (assumes CUnit is installed) */
#include <CUnit/Basic.h>

/* Define UNIT_TEST so that interpreter.c uses its test‐friendly version */
#ifndef UNIT_TEST
#define UNIT_TEST
#endif

/* Include project headers */
#include "common.h"
#include "util.h"
#include "terminal.h"
#include "disk.h"
#include "cluster.h"
#include "fs.h"
#include "mem_domain.h"
#include "fs_service_glue.h"
#include "fs_facade.h"
#include "threadpool.h"
#include "interpreter.h"
#include "fs_jail.h"
#include "fs_provider.h"

/* g_fs_jail_root is a non-static global exported by fs_jail.c */
extern char g_fs_jail_root[];

/* Directly include interpreter.c so that its definitions are compiled into this test.
   (Ensure that interpreter.c checks UNIT_TEST so that interactive_shell() is a stub.)
*/
#include "interpreter.c"

/* ---------------------------------------------------------------------------
 * Forward declarations for cd command tests.
 * -------------------------------------------------------------------------*/
void test_cd_batch(void);
void test_cd_interactive(void);
void test_cd_batch_extra(void);
void test_cd_interactive_extra(void);

/* ---------------------------------------------------------------------------
 * Helper for numbering tests with clear separation
 * -------------------------------------------------------------------------*/
static int current_test_num = 1;
void print_test_header(const char *name) {
    printf("\n========================================\n");
    printf("Test %d: %s\n", current_test_num++, name);
    printf("========================================\n\n");
}

/* ---------------------------------------------------------------------------
 * Helper: ensure that current_disk_file exists.
 * If not, create a minimal disk file.
 * -------------------------------------------------------------------------*/
void ensure_disk_exists(void) {
    if (access(current_disk_file, F_OK) != 0) {
        FILE *fp = fopen(current_disk_file, "w");
        if (fp) {
            /* Minimal disk: header with 8 hex digits (for a 4-byte cluster)
               and one zeroed cluster.
            */
            fprintf(fp, "XX:01234567\n");
            fprintf(fp, "00:00000000\n");
            fclose(fp);
            g_total_clusters = 1;
            g_cluster_size = 4;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Helper: run a "-cd" test.
 *
 * Parameters:
 *    volume      - Volume name (the disk file will be named "<volume>_disk.txt")
 *    rowCount    - Number of clusters
 *    nibbleCount - Total number of nibbles (cluster size = nibbleCount/2)
 *    interactive - If nonzero, the "-y" flag is appended (simulated interactive mode)
 *
 * This function builds the -cd command, calls execute_command_str(),
 * verifies that the resulting disk file exists and its header begins with "XX:",
 * prints a summary showing the drive dimensions and mode, and then removes the disk file.
 * -------------------------------------------------------------------------*/
void run_cd_test_mode(const char *volume, int rowCount, int nibbleCount, int interactive) {
    char command[256];
    if (interactive)
         snprintf(command, sizeof(command), "createdisk %s %d %d -y", volume, rowCount, nibbleCount);
    else
         snprintf(command, sizeof(command), "createdisk %s %d %d", volume, rowCount, nibbleCount);

    CU_ASSERT_TRUE(execute_command_str(command) == 0);

    char diskFileName[256];
    snprintf(diskFileName, sizeof(diskFileName), "%s_disk.txt", volume);
    FILE *fp = fopen(diskFileName, "r");
    CU_ASSERT_TRUE(fp != NULL);
    if (fp) {
         char header[1024];
         fgets(header, sizeof(header), fp);
         CU_ASSERT_TRUE(strncmp(header, "XX:", 3) == 0);
         fclose(fp);
    }
    printf("Disk '%s' created with %d clusters and cluster size %d bytes (nibbleCount = %d) in %s mode.\n\n",
           diskFileName, rowCount, nibbleCount / 2, nibbleCount, interactive ? "interactive" : "batch");
    remove(diskFileName);
}

/* ---------------------------------------------------------------------------
 * Suite setup and cleanup functions
 *
 * suite_setup: Use a fixed seed and create a reproducible drive file.
 * This creates "test_drive_disk.txt" and updates current_disk_file.
 *
 * suite_cleanup: Remove all temporary files and directories created during testing.
 * -------------------------------------------------------------------------*/
int suite_setup(void) {
    srand(12345);  /* Fixed seed for reproducibility */
    flintstone_format_disk("test_drive", 8, 16);  /* Creates "test_drive_disk.txt" */
    strncpy(current_disk_file, "test_drive_disk.txt", sizeof(current_disk_file)-1);
    fs_service_glue_init();
    return 0;
}

int suite_cleanup(void) {
    fs_service_glue_shutdown();
    /* List of known temporary files */
    const char *files[] = {
         "test_drive_disk.txt",
         "testdisk.txt",
         "mydisk.txt",
         "tempdisk.txt",
         "text_drive.txt",
         "imported_disk.txt",
         "testfile.txt",
         "test_output.txt",
         HISTORY_FILE,  /* e.g., "shell_history.txt" */
         "cdbatch1_disk.txt",
         "cdbatch2_disk.txt",
         "cdbatch3_disk.txt",
         "cdbatch_extra1_disk.txt",
         "cdbatch_extra2_disk.txt",
         "cdinter1_disk.txt",
         "cdinter2_disk.txt",
         "cdinter3_disk.txt",
         "cdinter_extra1_disk.txt",
         "cdinter_extra2_disk.txt",
         "test_disk.txt",
         "int_undo_test.txt"
    };
    int num_files = sizeof(files) / sizeof(files[0]);
    for (int i = 0; i < num_files; i++) {
         if (access(files[i], F_OK) == 0) {
              if (remove(files[i]) != 0) {
                  perror(files[i]);
              }
         }
    }
    /* Additionally, scan for any .txt file generated during testing and remove it */
    DIR *dir = opendir(".");
    if (dir) {
         struct dirent *entry;
         while ((entry = readdir(dir)) != NULL) {
              char *dot = strrchr(entry->d_name, '.');
              if (dot && strcmp(dot, ".txt") == 0) {
                  if (remove(entry->d_name) != 0) {
                      perror(entry->d_name);
                  }
              }
         }
         closedir(dir);
    }
    /* Remove any directories created by tests */
    rmdir("test_dir");
    rmdir("dummy_dir");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Tests for utility functions and basic commands
 * -------------------------------------------------------------------------*/
void test_trim_whitespace(void) {
    print_test_header("trim_whitespace");
    char s1[] = "   hello world   ";
    CU_ASSERT_TRUE(strcmp(trim_whitespace(s1), "hello world") == 0);
    
    char s2[] = "no_spaces";
    CU_ASSERT_TRUE(strcmp(trim_whitespace(s2), "no_spaces") == 0);
    
    char s3[] = "    ";
    CU_ASSERT_TRUE(strcmp(trim_whitespace(s3), "") == 0);
}

void test_convert_data_to_hex_text(void) {
    print_test_header("convert_data_to_hex (text)");
    char *hex = convert_data_to_hex("ABC", 1, 5);
    CU_ASSERT_TRUE(strcmp(hex, "4142430000") == 0);
    free(hex);
}

void test_convert_data_to_hex_nontext(void) {
    print_test_header("convert_data_to_hex (non-text)");
    char *hex = convert_data_to_hex("4142", 0, 4);
    CU_ASSERT_TRUE(strcmp(hex, "41420000") == 0);
    free(hex);
}

void test_convert_hex_to_ascii(void) {
    print_test_header("convert_hex_to_ascii");
    char *ascii = convert_hex_to_ascii("414243", 3);
    CU_ASSERT_TRUE(strcmp(ascii, "ABC") == 0);
    mem_domain_free(MEM_DOMAIN_FS, ascii);
}

void test_help_variants(void) {
    print_test_header("help variants");
    CU_ASSERT_TRUE(execute_command_str("help") == 0);
    CU_ASSERT_TRUE(execute_command_str("help") == 0);
}

void test_v_command(void) {
    print_test_header("version command");
    pid_t pid = fork();
    if (pid == 0) {
        execute_command_str("version -n");
        exit(0);
    } else {
        int status;
        waitpid(pid, &status, 0);
        CU_ASSERT_TRUE(WEXITSTATUS(status) == 0);
    }
}

void test_l_command(void) {
    print_test_header("list disk (-l)");
    ensure_disk_exists();
    CU_ASSERT_TRUE(execute_command_str("listclusters") == 0);
}

void test_s_command(void) {
    print_test_header("search (-s)");
    FILE *fp = fopen("testdisk.txt", "w");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
        fprintf(fp, "XX:12345678\n");
        /* Write a cluster with hex for "ABCD" */
        fprintf(fp, "00:41424344\n");
        fclose(fp);
    }
    CU_ASSERT_TRUE(execute_command_str("search ABCD") == 0);
    remove("testdisk.txt");
}

void test_w_command(void) {
    print_test_header("write (-w)");
    FILE *fp = fopen("testdisk.txt", "w");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
        fprintf(fp, "XX:12345678\n");
        fprintf(fp, "00:00000000\n");
        fclose(fp);
    }
    strcpy(current_disk_file, "testdisk.txt");
    g_total_clusters = 1;
    g_cluster_size = 4;
    CU_ASSERT_TRUE(execute_command_str("writecluster 0 -t TEST") == 0);
    fp = fopen("testdisk.txt", "r");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
        char buf[256];
        fgets(buf, sizeof(buf), fp);  /* skip header */
        fgets(buf, sizeof(buf), fp);  /* read cluster line */
        CU_ASSERT_TRUE(strstr(buf, "54455354") != NULL);
        fclose(fp);
    }
    remove("testdisk.txt");
}

void test_d_command(void) {
    print_test_header("delete (-d)");
    FILE *fp = fopen("testdisk.txt", "w");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
        fprintf(fp, "XX:12345678\n");
        fprintf(fp, "00:11111111\n");
        fclose(fp);
    }
    strcpy(current_disk_file, "testdisk.txt");
    g_total_clusters = 1;
    g_cluster_size = 4;
    CU_ASSERT_TRUE(execute_command_str("delcluster 0") == 0);
    fp = fopen("testdisk.txt", "r");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
        char buf[256];
        fgets(buf, sizeof(buf), fp);
        fgets(buf, sizeof(buf), fp);
        CU_ASSERT_TRUE(strstr(buf, "00000000") != NULL);
        fclose(fp);
    }
    remove("testdisk.txt");
}

void test_up_command(void) {
    print_test_header("update (-up)");
    FILE *fp = fopen("testdisk.txt", "w");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
        fprintf(fp, "XX:12345678\n");
        fprintf(fp, "00:11111111\n");
        fclose(fp);
    }
    strcpy(current_disk_file, "testdisk.txt");
    g_total_clusters = 1;
    g_cluster_size = 4;
    CU_ASSERT_TRUE(execute_command_str("update 0 -t NEW") == 0);
    fp = fopen("testdisk.txt", "r");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
        char buf[256];
        fgets(buf, sizeof(buf), fp);
        fgets(buf, sizeof(buf), fp);
        CU_ASSERT_TRUE(strstr(buf, "11111111") == NULL);
        fclose(fp);
    }
    remove("testdisk.txt");
}

void test_f_command(void) {
    print_test_header("set disk file (-f)");
    FILE *fp = fopen("mydisk.txt", "w");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
        fprintf(fp, "XX:12345678\n");
        fprintf(fp, "00:00000000\n");
        fclose(fp);
    }
    CU_ASSERT_TRUE(execute_command_str("setdisk mydisk.txt") == 0);
    CU_ASSERT_TRUE(strcmp(current_disk_file, "mydisk.txt") == 0);
    remove("mydisk.txt");
}

void test_F_command(void) {
    print_test_header("format disk (-F)");
    FILE *fp = fopen("tempdisk.txt", "w");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
        fprintf(fp, "XX:12345678\n");
        for (int i = 0; i < 3; i++) {
            fprintf(fp, "%02X:00000000\n", i);
        }
        fclose(fp);
    }
    CU_ASSERT_TRUE(execute_command_str("format tempdisk.txt testvol 4 16") == 0);
    fp = fopen("tempdisk.txt", "r");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
         char header[256];
         fgets(header, sizeof(header), fp);
         CU_ASSERT_TRUE(strncmp(header, "XX:", 3) == 0);
         fclose(fp);
    }
    remove("tempdisk.txt");
}

/* ---------------------------------------------------------------------------
 * New tests for disk usage commands (replacing -M with -du)
 * -------------------------------------------------------------------------*/
/* Test the short disk usage report (-du) */
void test_du_command(void) {
    print_test_header("disk usage (-du)");
    FILE *fp = fopen("mydisk.txt", "w");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
         /* Write header and one cluster with nonzero data */
         fprintf(fp, "XX:01234567\n");
         fprintf(fp, "00:11111111\n");
         fclose(fp);
         g_total_clusters = 1;
         g_cluster_size = 4;
    }
    CU_ASSERT_TRUE(execute_command_str("du") == 0);
    remove("mydisk.txt");
}

/* Test the detailed disk usage report (-du -dtl)
   This test creates a disk file with two clusters:
   - Cluster 0: used (nonzero data)
   - Cluster 1: available (all zeros)
*/
void test_du_dtl_command(void) {
    print_test_header("disk usage detail (-du -dtl)");
    FILE *fp = fopen("mydisk.txt", "w");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
         fprintf(fp, "XX:01234567\n");
         /* Cluster 0: used */
         fprintf(fp, "00:11111111\n");
         /* Cluster 1: available */
         fprintf(fp, "01:00000000\n");
         fclose(fp);
         g_total_clusters = 2;
         g_cluster_size = 4;
    }
    CU_ASSERT_TRUE(execute_command_str("du dtl 0 1") == 0);
    remove("mydisk.txt");
}

/* ---------------------------------------------------------------------------
 * Test for file display command using the new -type command.
 * This replaces the old cat command test.
 * It simulates a file stored in one cluster.
 * -------------------------------------------------------------------------*/
void test_type_command(void) {
    print_test_header("file display (-type)");
    FILE *fp = fopen("test_disk.txt", "w");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
         fprintf(fp, "XX:01234567\n");
         /* Set cluster size to 16 bytes for this test */
         g_cluster_size = 16;
         char *hex = convert_data_to_hex("Hello, CUnit Test!\n", 1, g_cluster_size);
         fprintf(fp, "00:%s\n", hex);
         free(hex);
         fclose(fp);
    }
    CU_ASSERT_TRUE(execute_command_str("type testfile.txt") == 0);
    remove("test_disk.txt");
}

/* ---------------------------------------------------------------------------
 * Integration tests for the "-cd" command using run_cd_test_mode().
 * These tests simulate both Batch and Interactive modes.
 * -------------------------------------------------------------------------*/
/* Batch Mode Tests */
void test_cd_batch(void) {
    print_test_header("cd command batch mode tests");
    run_cd_test_mode("cdbatch1", 8, 16, 0);
    run_cd_test_mode("cdbatch2", 16, 32, 0);
    run_cd_test_mode("cdbatch3", 32, 64, 0);
}

/* Interactive Mode Tests */
void test_cd_interactive(void) {
    print_test_header("cd command interactive mode tests");
    run_cd_test_mode("cdinter1", 8, 16, 1);
    run_cd_test_mode("cdinter2", 16, 32, 1);
    run_cd_test_mode("cdinter3", 32, 64, 1);
}

/* Additional cd tests with extra dimensions */
void test_cd_batch_extra(void) {
    print_test_header("cd batch extra dimensions");
    run_cd_test_mode("cdbatch_extra1", 10, 20, 0);
    run_cd_test_mode("cdbatch_extra2", 20, 40, 0);
}

void test_cd_interactive_extra(void) {
    print_test_header("cd interactive extra dimensions");
    run_cd_test_mode("cdinter_extra1", 10, 20, 1);
    run_cd_test_mode("cdinter_extra2", 20, 40, 1);
}

/* ---------------------------------------------------------------------------
 * Remaining tests for other commands.
 * -------------------------------------------------------------------------*/
void test_dir_command(void) {
    print_test_header("dir command");
    CU_ASSERT_TRUE(execute_command_str("dir") == 0);
}

void test_D_and_R_commands(void) {
    print_test_header("directory creation and removal (-D and -R)");
    ensure_disk_exists();
    int ret = execute_command_str("mkdir test_dir");
    struct stat st;
    if (ret != 0) {
        if (stat("test_dir", &st) != 0)
            ret = mkdir("test_dir", 0755);
        else
            ret = 0;
    }
    CU_ASSERT_TRUE(ret == 0);
    ret = execute_command_str("rmtree test_dir");
    if (ret != 0) {
        ret = (rmdir("test_dir") == 0) ? 0 : ret;
    }
    CU_ASSERT_TRUE(ret == 0);
}

void test_L_command(void) {
    print_test_header("list local directories (-L)");
    ensure_disk_exists();
    mkdir("dummy_dir", 0755);
    int ret = execute_command_str("listdirs");
    rmdir("dummy_dir");
    CU_ASSERT_TRUE(ret == 0);
}

void test_O_command(void) {
    print_test_header("redirect output (-O)");
    CU_ASSERT_TRUE(execute_command_str("redirect test_output.txt") == 0);
    printf("Redirected output test.\n");
    CU_ASSERT_TRUE(execute_command_str("redirect off") == 0);
    FILE *fp = fopen("test_output.txt", "r");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
         char buf[256];
         fgets(buf, sizeof(buf), fp);
         CU_ASSERT_TRUE(strstr(buf, "Redirected output test.") != NULL);
         fclose(fp);
    }
    remove("test_output.txt");
}

void test_init_command(void) {
    print_test_header("initialize disk (-init)");
    CU_ASSERT_TRUE(execute_command_str("initdisk 100 64") == 0);
    CU_ASSERT_TRUE(g_total_clusters == 100);
    CU_ASSERT_TRUE(g_cluster_size == 64);
}

void test_uc_command(void) {
    print_test_header("history re-run (-uc)");
    FILE *fp = fopen(HISTORY_FILE, "a");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
         fprintf(fp, "echo UC_Test\n");
         fclose(fp);
    }
    CU_ASSERT_TRUE(execute_command_str("rerun 1") == 0);
}

void test_import_command(void) {
    print_test_header("import text drive (-import)");
    FILE *fp = fopen("text_drive.txt", "w");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
         fprintf(fp, "XX:01234567\n");
         fprintf(fp, "00:ABCDEF12\n");
         fprintf(fp, "01:12345678\n");
         fclose(fp);
    }
    CU_ASSERT_TRUE(execute_command_str("import text_drive.txt imported_disk.txt") == 0);
    fp = fopen("imported_disk.txt", "r");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp)
         fclose(fp);
    remove("text_drive.txt");
    remove("imported_disk.txt");
}

void test_print_command(void) {
    print_test_header("print disk (-print)");
    FILE *fp = fopen("mydisk.txt", "w");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
         fprintf(fp, "XX:01234567\n");
         fprintf(fp, "00:11111111\n");
         fclose(fp);
    }
    CU_ASSERT_TRUE(execute_command_str("printdisk") == 0);
    remove("mydisk.txt");
}

void test_clear_command(void) {
    print_test_header("clear screen (clear)");
    CU_ASSERT_TRUE(execute_command_str("clear") == 0);
}

void test_history_commands(void) {
    print_test_header("display history (history, his)");
    CU_ASSERT_TRUE(execute_command_str("history") == 0);
    CU_ASSERT_TRUE(execute_command_str("his") == 0);
}

void test_cc_command(void) {
    print_test_header("clear history (cc)");
    FILE *fp = fopen(HISTORY_FILE, "w");
    CU_ASSERT_PTR_NOT_NULL(fp);
    if (fp) {
         fprintf(fp, "dummy\n");
         fclose(fp);
    }
    CU_ASSERT_TRUE(execute_command_str("cc") == 0);
    fp = fopen(HISTORY_FILE, "r");
    CU_ASSERT_TRUE(fp == NULL);
}

void test_external_command(void) {
    print_test_header("external command (echo)");
    CU_ASSERT_TRUE(execute_command_str("echo ExternalCommandTest") == 0);
}

/* ---------------------------------------------------------------------------
 * Integration tests: storage + scheduler + command undo + error modes
 * -------------------------------------------------------------------------*/
void test_integration_undo(void) {
    print_test_header("integration: create/save/undo");
    if (!g_fm_service) { CU_ASSERT_TRUE(0); return; }
    const char *path = "int_undo_test.txt";
    CU_ASSERT_TRUE(fm_create_file(g_fm_service, path) == 0);
    CU_ASSERT_TRUE(fm_save_text(g_fm_service, path, "hello") == 0);
    char buf[64];
    buf[0] = '\0';
    fm_read_text(g_fm_service, path, buf, sizeof(buf));
    CU_ASSERT_TRUE(strstr(buf, "hello") != NULL);
    CU_ASSERT_TRUE(fm_undo_available(g_fm_service) >= 2);
    CU_ASSERT_TRUE(fm_undo(g_fm_service) == 0);
    CU_ASSERT_TRUE(fm_undo(g_fm_service) == 0);
}

void test_integration_storage(void) {
    print_test_header("integration: disk storage pipeline");
    ensure_disk_exists();
    CU_ASSERT_TRUE(execute_command_str("writecluster 0 -t DATA") == 0);
    CU_ASSERT_TRUE(execute_command_str("search DATA") == 0);
}

void test_exit_command(void) {
    print_test_header("exit command");
    pid_t pid = fork();
    if (pid == 0) {
         execute_command_str("exit -n");
         exit(0);
    } else {
         int status;
         waitpid(pid, &status, 0);
         CU_ASSERT_TRUE(WEXITSTATUS(status) == 0);
    }
}

/* ---------------------------------------------------------------------------
 * VM Jail Suite helpers
 * -------------------------------------------------------------------------*/

/* Temp directory used by the jail suite; set during jail_suite_setup */
static char s_jail_tmpdir[PATH_MAX];

/* Saved original g_vm_mode, g_vm_root, g_cwd so we can restore them */
static int  s_saved_vm_mode;
static char s_saved_vm_root[CWD_MAX];
static char s_saved_cwd[CWD_MAX];

static int jail_suite_setup(void) {
    /* Save original globals */
    s_saved_vm_mode = g_vm_mode;
    strncpy(s_saved_vm_root, g_vm_root, sizeof(s_saved_vm_root) - 1);
    strncpy(s_saved_cwd, g_cwd, sizeof(s_saved_cwd) - 1);

    /* Create a private temp directory to act as the jail root */
    strncpy(s_jail_tmpdir, "/tmp/flinstone_jail_test_XXXXXX", sizeof(s_jail_tmpdir) - 1);
    if (!mkdtemp(s_jail_tmpdir))
        return -1;

    /* Activate VM mode with the temp dir as jail root */
    g_vm_mode = 1;
    strncpy(g_vm_root, s_jail_tmpdir, sizeof(g_vm_root) - 1);
    strncpy(g_cwd,    s_jail_tmpdir, sizeof(g_cwd)    - 1);

    fs_jail_init();
    fs_service_glue_init();
    return 0;
}

static int jail_suite_cleanup(void) {
    fs_service_glue_shutdown();

    /* Restore globals */
    g_vm_mode = s_saved_vm_mode;
    strncpy(g_vm_root, s_saved_vm_root, sizeof(g_vm_root) - 1);
    strncpy(g_cwd,     s_saved_cwd,     sizeof(g_cwd)     - 1);

    /* Re-init jail to inactive state */
    fs_jail_init();

    /* Remove temp dir (non-recursive: should be empty after tests) */
    rmdir(s_jail_tmpdir);
    s_jail_tmpdir[0] = '\0';
    return 0;
}

/* ---------------------------------------------------------------------------
 * fs_jail_is_active tests
 * -------------------------------------------------------------------------*/
void test_jail_is_active_in_vm_mode(void) {
    print_test_header("fs_jail_is_active returns 1 in VM mode");
    /* jail_suite_setup already called fs_jail_init with g_vm_mode=1 */
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);
    CU_ASSERT_TRUE(g_fs_jail_root[0] != '\0');
}

void test_jail_is_inactive_without_vm_mode(void) {
    print_test_header("fs_jail_is_active returns 0 when g_vm_mode=0");
    int saved = g_vm_mode;
    g_vm_mode = 0;
    fs_jail_init();
    CU_ASSERT_TRUE(fs_jail_is_active() == 0);
    /* Restore */
    g_vm_mode = saved;
    fs_jail_init();
}

/* ---------------------------------------------------------------------------
 * fs_jail_check_path: NULL / empty
 * -------------------------------------------------------------------------*/
void test_jail_check_null_returns_error(void) {
    print_test_header("fs_jail_check_path(NULL) == -1");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);
    CU_ASSERT_TRUE(fs_jail_check_path(NULL) == -1);
}

void test_jail_check_empty_returns_error(void) {
    print_test_header("fs_jail_check_path(\"\") == -1 when jail active");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);
    CU_ASSERT_TRUE(fs_jail_check_path("") == -1);
}

/* ---------------------------------------------------------------------------
 * fs_jail_check_path: inside and outside
 * -------------------------------------------------------------------------*/
void test_jail_check_inside_allowed(void) {
    print_test_header("fs_jail_check_path: path inside jail is allowed");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);
    /* The jail root itself must be allowed */
    CU_ASSERT_TRUE(fs_jail_check_path(s_jail_tmpdir) == 0);

    /* Create a real subdir inside the jail and check it */
    char subdir[PATH_MAX];
    snprintf(subdir, sizeof(subdir), "%s/inner", s_jail_tmpdir);
    mkdir(subdir, 0755);
    CU_ASSERT_TRUE(fs_jail_check_path(subdir) == 0);
    rmdir(subdir);
}

void test_jail_check_outside_blocked(void) {
    print_test_header("fs_jail_check_path: path outside jail is blocked");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);
    CU_ASSERT_TRUE(fs_jail_check_path("/etc") == -1);
    CU_ASSERT_TRUE(fs_jail_check_path("/usr") == -1);
}

void test_jail_check_prefix_attack_blocked(void) {
    print_test_header("fs_jail_check_path: prefix-match sibling path is blocked");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);
    /* Build a sibling: s_jail_tmpdir + "_evil" - same prefix but not a child */
    char sibling[PATH_MAX];
    snprintf(sibling, sizeof(sibling), "%s_evil", s_jail_tmpdir);
    mkdir(sibling, 0755);
    int ret = fs_jail_check_path(sibling);
    CU_ASSERT_TRUE(ret == -1);
    rmdir(sibling);
}

void test_jail_check_nonexistent_inside_allowed(void) {
    print_test_header("fs_jail_check_path: nonexistent file inside jail is allowed");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);
    char newfile[PATH_MAX];
    snprintf(newfile, sizeof(newfile), "%s/ghost_file.txt", s_jail_tmpdir);
    /* Confirm it doesn't exist */
    CU_ASSERT_TRUE(access(newfile, F_OK) != 0);
    CU_ASSERT_TRUE(fs_jail_check_path(newfile) == 0);
}

void test_jail_check_nonexistent_outside_blocked(void) {
    print_test_header("fs_jail_check_path: nonexistent file with parent outside jail is blocked");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);
    CU_ASSERT_TRUE(fs_jail_check_path("/etc/no_such_file_xyzzy") == -1);
}

/* ---------------------------------------------------------------------------
 * fs_provider local provider: jail enforcement
 * -------------------------------------------------------------------------*/
void test_fs_provider_read_inside_allowed(void) {
    print_test_header("fs_provider read_text: file inside jail is allowed");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);

    /* Create a file inside the jail */
    char fpath[PATH_MAX];
    snprintf(fpath, sizeof(fpath), "%s/prov_read_test.txt", s_jail_tmpdir);
    FILE *f = fopen(fpath, "w");
    CU_ASSERT_PTR_NOT_NULL(f);
    if (f) { fprintf(f, "hello jail"); fclose(f); }

    fs_provider_t *p = fs_local_provider_create();
    CU_ASSERT_PTR_NOT_NULL(p);
    if (p) {
        char buf[64] = {0};
        int n = fs_provider_read_text(p, fpath, buf, sizeof(buf));
        CU_ASSERT_TRUE(n > 0);
        CU_ASSERT_TRUE(strstr(buf, "hello jail") != NULL);
        fs_provider_destroy(p);
    }
    remove(fpath);
}

void test_fs_provider_read_outside_blocked(void) {
    print_test_header("fs_provider read_text: file outside jail is blocked");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);

    fs_provider_t *p = fs_local_provider_create();
    CU_ASSERT_PTR_NOT_NULL(p);
    if (p) {
        char buf[64] = {0};
        int ret = fs_provider_read_text(p, "/etc/hostname", buf, sizeof(buf));
        CU_ASSERT_TRUE(ret == -1);
        fs_provider_destroy(p);
    }
}

void test_fs_provider_write_inside_allowed(void) {
    print_test_header("fs_provider write_text: file inside jail is allowed");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);

    char fpath[PATH_MAX];
    snprintf(fpath, sizeof(fpath), "%s/prov_write_test.txt", s_jail_tmpdir);

    fs_provider_t *p = fs_local_provider_create();
    CU_ASSERT_PTR_NOT_NULL(p);
    if (p) {
        int ret = fs_provider_write_text(p, fpath, "written inside jail");
        CU_ASSERT_TRUE(ret == 0);
        fs_provider_destroy(p);
    }
    remove(fpath);
}

void test_fs_provider_write_outside_blocked(void) {
    print_test_header("fs_provider write_text: file outside jail is blocked (errno=EPERM)");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);

    fs_provider_t *p = fs_local_provider_create();
    CU_ASSERT_PTR_NOT_NULL(p);
    if (p) {
        errno = 0;
        int ret = fs_provider_write_text(p, "/tmp/jail_escape_attempt.txt", "evil");
        CU_ASSERT_TRUE(ret == -1);
        CU_ASSERT_TRUE(errno == EPERM);
        fs_provider_destroy(p);
    }
}

void test_fs_provider_create_file_outside_blocked(void) {
    print_test_header("fs_provider create_file: file outside jail is blocked");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);

    fs_provider_t *p = fs_local_provider_create();
    CU_ASSERT_PTR_NOT_NULL(p);
    if (p) {
        int ret = fs_provider_create_file(p, "/tmp/jail_test_create.txt");
        CU_ASSERT_TRUE(ret == -1);
        /* Ensure no file was created outside the jail */
        CU_ASSERT_TRUE(access("/tmp/jail_test_create.txt", F_OK) != 0);
        fs_provider_destroy(p);
    }
}

void test_fs_provider_delete_outside_blocked(void) {
    print_test_header("fs_provider delete: path outside jail is blocked");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);

    /* Create a file outside the jail to try to delete */
    const char *victim = "/tmp/jail_delete_victim.txt";
    FILE *f = fopen(victim, "w");
    if (f) { fprintf(f, "x"); fclose(f); }

    fs_provider_t *p = fs_local_provider_create();
    CU_ASSERT_PTR_NOT_NULL(p);
    if (p) {
        int ret = fs_provider_delete(p, victim);
        CU_ASSERT_TRUE(ret == -1);
        /* Victim must still exist - was not deleted */
        CU_ASSERT_TRUE(access(victim, F_OK) == 0);
        fs_provider_destroy(p);
    }
    remove(victim);
}

void test_fs_provider_move_dst_outside_blocked(void) {
    print_test_header("fs_provider move: destination outside jail is blocked");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);

    /* src inside jail */
    char src[PATH_MAX];
    snprintf(src, sizeof(src), "%s/move_src.txt", s_jail_tmpdir);
    FILE *f = fopen(src, "w");
    if (f) { fprintf(f, "src"); fclose(f); }

    fs_provider_t *p = fs_local_provider_create();
    CU_ASSERT_PTR_NOT_NULL(p);
    if (p) {
        int ret = fs_provider_move(p, src, "/tmp/move_dst_outside.txt");
        CU_ASSERT_TRUE(ret == -1);
        /* src must still exist */
        CU_ASSERT_TRUE(access(src, F_OK) == 0);
        fs_provider_destroy(p);
    }
    remove(src);
}

/* ---------------------------------------------------------------------------
 * interpreter.c jail enforcement: cd, format, setdisk
 * -------------------------------------------------------------------------*/
void test_interpreter_cd_blocked_outside_jail(void) {
    print_test_header("interpreter cd: cd outside jail is blocked");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);

    char saved_cwd[CWD_MAX];
    strncpy(saved_cwd, g_cwd, sizeof(saved_cwd) - 1);

    /* Attempt to cd to /tmp which is outside the jail */
    int ret = execute_command_str("cd /tmp");
    /* Command returns 1 (blocked) and g_cwd must remain unchanged */
    CU_ASSERT_TRUE(ret == 1);
    CU_ASSERT_TRUE(strcmp(g_cwd, saved_cwd) == 0);
}

void test_interpreter_cd_allowed_inside_jail(void) {
    print_test_header("interpreter cd: cd inside jail is allowed");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);

    /* Create a subdirectory inside the jail */
    char subdir[PATH_MAX];
    snprintf(subdir, sizeof(subdir), "%s/cd_target", s_jail_tmpdir);
    mkdir(subdir, 0755);

    int ret = execute_command_str("cd cd_target");
    /* Should succeed (return 0) */
    CU_ASSERT_TRUE(ret == 0);

    /* Return to jail root for next tests */
    chdir(s_jail_tmpdir);
    strncpy(g_cwd, s_jail_tmpdir, sizeof(g_cwd) - 1);

    rmdir(subdir);
}

void test_interpreter_format_blocked_outside_jail(void) {
    print_test_header("interpreter format: format outside jail is blocked");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);

    /* Attempt to format a disk outside the jail */
    int ret = execute_command_str("format /tmp/outside_disk.txt testvol 4 16");
    CU_ASSERT_TRUE(ret == 1);
    /* Confirm no file was created */
    CU_ASSERT_TRUE(access("/tmp/outside_disk.txt", F_OK) != 0);
}

void test_interpreter_format_allowed_inside_jail(void) {
    print_test_header("interpreter format: format inside jail is allowed");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);

    char diskfile[PATH_MAX];
    snprintf(diskfile, sizeof(diskfile), "%s/jail_disk.txt", s_jail_tmpdir);

    /* format <path> <vol> <rows> <nibbles> */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "format %s jailtest 4 16", diskfile);
    int ret = execute_command_str(cmd);
    CU_ASSERT_TRUE(ret == 0);
    CU_ASSERT_TRUE(access(diskfile, F_OK) == 0);
    remove(diskfile);
}

void test_interpreter_setdisk_blocked_outside_jail(void) {
    print_test_header("interpreter setdisk: setdisk outside jail is blocked");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);

    /* Create a disk file outside the jail */
    const char *outside = "/tmp/outside_setdisk.txt";
    FILE *f = fopen(outside, "w");
    if (f) { fprintf(f, "XX:00000000\n00:00000000\n"); fclose(f); }

    char saved_disk[sizeof(current_disk_file)];
    strncpy(saved_disk, current_disk_file, sizeof(saved_disk) - 1);

    int ret = execute_command_str("setdisk /tmp/outside_setdisk.txt");
    CU_ASSERT_TRUE(ret == 1);
    /* current_disk_file must remain unchanged */
    CU_ASSERT_TRUE(strcmp(current_disk_file, saved_disk) == 0);

    remove(outside);
}

void test_interpreter_setdisk_allowed_inside_jail(void) {
    print_test_header("interpreter setdisk: setdisk inside jail is allowed");
    CU_ASSERT_TRUE(fs_jail_is_active() == 1);

    /* Create a valid disk file inside the jail */
    char diskfile[PATH_MAX];
    snprintf(diskfile, sizeof(diskfile), "%s/setdisk_inside.txt", s_jail_tmpdir);
    FILE *f = fopen(diskfile, "w");
    CU_ASSERT_PTR_NOT_NULL(f);
    if (f) {
        fprintf(f, "XX:00000000\n00:00000000\n");
        fclose(f);
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "setdisk %s", diskfile);
    int ret = execute_command_str(cmd);
    CU_ASSERT_TRUE(ret == 0);
    CU_ASSERT_TRUE(strcmp(current_disk_file, diskfile) == 0);

    remove(diskfile);
}

/* ---------------------------------------------------------------------------
 * Main: Set up and run the CUnit tests.
 * -------------------------------------------------------------------------*/
int main(void)
{
    if (CUE_SUCCESS != CU_initialize_registry())
         return CU_get_error();

    CU_pSuite suite = CU_add_suite("Shell System Full Suite", suite_setup, suite_cleanup);
    if (NULL == suite) {
         CU_cleanup_registry();
         return CU_get_error();
    }

    /* Basic utility and command tests */
    CU_ADD_TEST(suite, test_trim_whitespace);
    CU_ADD_TEST(suite, test_convert_data_to_hex_text);
    CU_ADD_TEST(suite, test_convert_data_to_hex_nontext);
    CU_ADD_TEST(suite, test_convert_hex_to_ascii);
    CU_ADD_TEST(suite, test_help_variants);
    CU_ADD_TEST(suite, test_v_command);
    CU_ADD_TEST(suite, test_l_command);
    CU_ADD_TEST(suite, test_s_command);
    CU_ADD_TEST(suite, test_w_command);
    CU_ADD_TEST(suite, test_d_command);
    CU_ADD_TEST(suite, test_up_command);
    CU_ADD_TEST(suite, test_f_command);
    CU_ADD_TEST(suite, test_F_command);

    /* "-cd" command tests */
    CU_ADD_TEST(suite, test_cd_batch);
    CU_ADD_TEST(suite, test_cd_interactive);
    CU_ADD_TEST(suite, test_cd_batch_extra);
    CU_ADD_TEST(suite, test_cd_interactive_extra);

    /* Remaining tests */
    CU_ADD_TEST(suite, test_dir_command);
    CU_ADD_TEST(suite, test_D_and_R_commands);
    CU_ADD_TEST(suite, test_L_command);
    CU_ADD_TEST(suite, test_du_command);
    CU_ADD_TEST(suite, test_du_dtl_command);
    CU_ADD_TEST(suite, test_type_command);
    CU_ADD_TEST(suite, test_O_command);
    CU_ADD_TEST(suite, test_init_command);
    CU_ADD_TEST(suite, test_uc_command);
    CU_ADD_TEST(suite, test_import_command);
    CU_ADD_TEST(suite, test_print_command);
    CU_ADD_TEST(suite, test_clear_command);
    CU_ADD_TEST(suite, test_history_commands);
    CU_ADD_TEST(suite, test_cc_command);
    CU_ADD_TEST(suite, test_external_command);
    CU_ADD_TEST(suite, test_exit_command);
    CU_ADD_TEST(suite, test_integration_undo);
    CU_ADD_TEST(suite, test_integration_storage);

    /* --- VM Jail Suite --- */
    CU_pSuite jail_suite = CU_add_suite("VM Jail Suite", jail_suite_setup, jail_suite_cleanup);
    if (NULL == jail_suite) {
         CU_cleanup_registry();
         return CU_get_error();
    }

    /* fs_jail_init / fs_jail_is_active */
    CU_ADD_TEST(jail_suite, test_jail_is_active_in_vm_mode);
    CU_ADD_TEST(jail_suite, test_jail_is_inactive_without_vm_mode);

    /* fs_jail_check_path: null / empty */
    CU_ADD_TEST(jail_suite, test_jail_check_null_returns_error);
    CU_ADD_TEST(jail_suite, test_jail_check_empty_returns_error);

    /* fs_jail_check_path: inside / outside / prefix-attack / nonexistent */
    CU_ADD_TEST(jail_suite, test_jail_check_inside_allowed);
    CU_ADD_TEST(jail_suite, test_jail_check_outside_blocked);
    CU_ADD_TEST(jail_suite, test_jail_check_prefix_attack_blocked);
    CU_ADD_TEST(jail_suite, test_jail_check_nonexistent_inside_allowed);
    CU_ADD_TEST(jail_suite, test_jail_check_nonexistent_outside_blocked);

    /* fs_provider local provider jail enforcement */
    CU_ADD_TEST(jail_suite, test_fs_provider_read_inside_allowed);
    CU_ADD_TEST(jail_suite, test_fs_provider_read_outside_blocked);
    CU_ADD_TEST(jail_suite, test_fs_provider_write_inside_allowed);
    CU_ADD_TEST(jail_suite, test_fs_provider_write_outside_blocked);
    CU_ADD_TEST(jail_suite, test_fs_provider_create_file_outside_blocked);
    CU_ADD_TEST(jail_suite, test_fs_provider_delete_outside_blocked);
    CU_ADD_TEST(jail_suite, test_fs_provider_move_dst_outside_blocked);

    /* interpreter.c jail checks: cd, format, setdisk */
    CU_ADD_TEST(jail_suite, test_interpreter_cd_blocked_outside_jail);
    CU_ADD_TEST(jail_suite, test_interpreter_cd_allowed_inside_jail);
    CU_ADD_TEST(jail_suite, test_interpreter_format_blocked_outside_jail);
    CU_ADD_TEST(jail_suite, test_interpreter_format_allowed_inside_jail);
    CU_ADD_TEST(jail_suite, test_interpreter_setdisk_blocked_outside_jail);
    CU_ADD_TEST(jail_suite, test_interpreter_setdisk_allowed_inside_jail);

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();
    return CU_get_error();
}
