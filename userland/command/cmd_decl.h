#ifndef CMD_DECL_H
#define CMD_DECL_H

#include "fl_shell_cmd.h"

/* Line-mode builtins (parsed before argv split). */
/* Returns 1 if "exit" was recognized (including usage errors); 0 if unrelated line. */
int cmd_exit_maybe(char *trimmed);
int cmd_clear_maybe(const char *trimmed);
int cmd_help_maybe(const char *trimmed);
int cmd_history_maybe(const char *trimmed);
int cmd_cc_maybe(const char *trimmed);
int cmd_bios_maybe(char *trimmed);
int cmd_make_maybe(char *trimmed);

fl_shell_cmd_no_t fl_shell_cmd_lookup(const char *name);
int fl_shell_cmd_dispatch(fl_shell_cmd_no_t no, int argc, char **argv);

int cmd_cd_run(int argc, char **argv);
int cmd_createdisk_run(int argc, char **argv);
int cmd_format_run(int argc, char **argv);
int cmd_setdisk_run(int argc, char **argv);
int cmd_version_run(int argc, char **argv);
int cmd_contracts_run(int argc, char **argv);
int cmd_audit_run(int argc, char **argv);
int cmd_listclusters_run(int argc, char **argv);
int cmd_listdirs_run(int argc, char **argv);
int cmd_search_run(int argc, char **argv);
int cmd_writecluster_run(int argc, char **argv);
int cmd_delcluster_run(int argc, char **argv);
int cmd_update_run(int argc, char **argv);
int cmd_cat_run(int argc, char **argv);
int cmd_redirect_run(int argc, char **argv);
int cmd_initdisk_run(int argc, char **argv);
int cmd_rerun_run(int argc, char **argv);
int cmd_import_run(int argc, char **argv);
int cmd_du_run(int argc, char **argv);
int cmd_printdisk_run(int argc, char **argv);
int cmd_dir_run(int argc, char **argv);
int cmd_mkdir_run(int argc, char **argv);
int cmd_rmdir_run(int argc, char **argv);
int cmd_rmtree_run(int argc, char **argv);
int cmd_write_run(int argc, char **argv);
int cmd_mv_run(int argc, char **argv);
int cmd_where_run(int argc, char **argv);
int cmd_addcluster_run(int argc, char **argv);
int cmd_diskput_run(int argc, char **argv);
int cmd_diskget_run(int argc, char **argv);
int cmd_diskfiles_run(int argc, char **argv);
int cmd_diskdel_run(int argc, char **argv);
int cmd_diskmkdir_run(int argc, char **argv);
int cmd_sudo_run(int argc, char **argv);
int cmd_login_run(int argc, char **argv);
int cmd_useradd_run(int argc, char **argv);
int cmd_userdel_run(int argc, char **argv);
int cmd_passwd_run(int argc, char **argv);

#endif /* CMD_DECL_H */
