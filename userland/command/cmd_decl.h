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
int cmd_sudo_interactive_login(void);
int cmd_su_run(int argc, char **argv);
int cmd_login_run(int argc, char **argv);
int cmd_logout_run(int argc, char **argv);
int cmd_useradd_run(int argc, char **argv);
int cmd_userdel_run(int argc, char **argv);
int cmd_passwd_run(int argc, char **argv);
int cmd_whoami_run(int argc, char **argv);
int cmd_ping_run(int argc, char **argv);
int cmd_ping6_run(int argc, char **argv);
int cmd_ping6_batch_tokens_count(int argc, char **argv, int i);
int cmd_check_run(int argc, char **argv);
int cmd_server_run(int argc, char **argv);
int cmd_udpsend_run(int argc, char **argv);
int cmd_udplisten_run(int argc, char **argv);
int cmd_arp_run(int argc, char **argv);
int cmd_ifconfig_run(int argc, char **argv);
int cmd_route_run(int argc, char **argv);
int cmd_netstat_run(int argc, char **argv);
int cmd_nslookup_run(int argc, char **argv);
int cmd_resolve_run(int argc, char **argv);
int cmd_netsh_run(int argc, char **argv);
int cmd_wifi_run(int argc, char **argv);
int cmd_wifi_batch_tokens_count(int argc, char **argv, int i);
int cmd_dhcp_run(int argc, char **argv);

/*
 * Batch argv token counts (see cmd_batch.h).
 * Most commands are reached only via fl_shell_cmd_batch_tokens_count() in
 * cmd_batch_dispatch.c (called from cmd_batch.c); static analysis may
 * report the per-command helpers as unused even though the switch calls them.
 */
int fl_shell_cmd_batch_tokens_count(fl_shell_cmd_no_t no, int argc, char **argv, int i);
int cmd_help_batch_tokens_count(int argc, char **argv, int i);
int cmd_clear_batch_tokens_count(int argc, char **argv, int i);
int cmd_history_batch_tokens_count(int argc, char **argv, int i);
int cmd_cc_batch_tokens_count(int argc, char **argv, int i);
int cmd_exit_batch_tokens_count(int argc, char **argv, int i);
int cmd_bios_batch_tokens_count(int argc, char **argv, int i);
int cmd_make_batch_tokens_count(int argc, char **argv, int i);
int cmd_cd_batch_tokens_count(int argc, char **argv, int i);
int cmd_createdisk_batch_tokens_count(int argc, char **argv, int i);
int cmd_format_batch_tokens_count(int argc, char **argv, int i);
int cmd_setdisk_batch_tokens_count(int argc, char **argv, int i);
int cmd_version_batch_tokens_count(int argc, char **argv, int i);
int cmd_contracts_batch_tokens_count(int argc, char **argv, int i);
int cmd_audit_batch_tokens_count(int argc, char **argv, int i);
int cmd_listclusters_batch_tokens_count(int argc, char **argv, int i);
int cmd_listdirs_batch_tokens_count(int argc, char **argv, int i);
int cmd_search_batch_tokens_count(int argc, char **argv, int i);
int cmd_writecluster_batch_tokens_count(int argc, char **argv, int i);
int cmd_delcluster_batch_tokens_count(int argc, char **argv, int i);
int cmd_update_batch_tokens_count(int argc, char **argv, int i);
int cmd_cat_batch_tokens_count(int argc, char **argv, int i);
int cmd_redirect_batch_tokens_count(int argc, char **argv, int i);
int cmd_initdisk_batch_tokens_count(int argc, char **argv, int i);
int cmd_rerun_batch_tokens_count(int argc, char **argv, int i);
int cmd_import_batch_tokens_count(int argc, char **argv, int i);
int cmd_du_batch_tokens_count(int argc, char **argv, int i);
int cmd_printdisk_batch_tokens_count(int argc, char **argv, int i);
int cmd_dir_batch_tokens_count(int argc, char **argv, int i);
int cmd_mkdir_batch_tokens_count(int argc, char **argv, int i);
int cmd_rmdir_batch_tokens_count(int argc, char **argv, int i);
int cmd_rmtree_batch_tokens_count(int argc, char **argv, int i);
int cmd_write_batch_tokens_count(int argc, char **argv, int i);
int cmd_mv_batch_tokens_count(int argc, char **argv, int i);
int cmd_where_batch_tokens_count(int argc, char **argv, int i);
int cmd_addcluster_batch_tokens_count(int argc, char **argv, int i);
int cmd_diskput_batch_tokens_count(int argc, char **argv, int i);
int cmd_diskget_batch_tokens_count(int argc, char **argv, int i);
int cmd_diskfiles_batch_tokens_count(int argc, char **argv, int i);
int cmd_diskdel_batch_tokens_count(int argc, char **argv, int i);
int cmd_diskmkdir_batch_tokens_count(int argc, char **argv, int i);
int cmd_sudo_batch_tokens_count(int argc, char **argv, int i);
int cmd_su_batch_tokens_count(int argc, char **argv, int i);
int cmd_login_batch_tokens_count(int argc, char **argv, int i);
int cmd_logout_batch_tokens_count(int argc, char **argv, int i);
int cmd_useradd_batch_tokens_count(int argc, char **argv, int i);
int cmd_userdel_batch_tokens_count(int argc, char **argv, int i);
int cmd_passwd_batch_tokens_count(int argc, char **argv, int i);
int cmd_whoami_batch_tokens_count(int argc, char **argv, int i);
/* Indirect: fl_shell_cmd_batch_tokens_count(FL_SCMD_PING, ...) in cmd_batch_dispatch.c */
int cmd_ping_batch_tokens_count(int argc, char **argv, int i);
/* Indirect: fl_shell_cmd_batch_tokens_count(FL_SCMD_CHECK, ...) in cmd_batch_dispatch.c */
int cmd_check_batch_tokens_count(int argc, char **argv, int i);
/* Indirect: fl_shell_cmd_batch_tokens_count(FL_SCMD_SERVER, ...) in cmd_batch_dispatch.c */
int cmd_server_batch_tokens_count(int argc, char **argv, int i);
/* Indirect: fl_shell_cmd_batch_tokens_count(FL_SCMD_UDPSEND, ...) in cmd_batch_dispatch.c */
int cmd_udpsend_batch_tokens_count(int argc, char **argv, int i);
/* Indirect: fl_shell_cmd_batch_tokens_count(FL_SCMD_UDPLISTEN, ...) in cmd_batch_dispatch.c */
int cmd_udplisten_batch_tokens_count(int argc, char **argv, int i);
/* Indirect: fl_shell_cmd_batch_tokens_count(FL_SCMD_ARP, ...) in cmd_batch_dispatch.c */
int cmd_arp_batch_tokens_count(int argc, char **argv, int i);
/* Indirect: fl_shell_cmd_batch_tokens_count(FL_SCMD_IFCONFIG, ...) in cmd_batch_dispatch.c */
int cmd_ifconfig_batch_tokens_count(int argc, char **argv, int i);
/* Indirect: fl_shell_cmd_batch_tokens_count(FL_SCMD_ROUTE, ...) in cmd_batch_dispatch.c */
int cmd_route_batch_tokens_count(int argc, char **argv, int i);
/* Indirect: fl_shell_cmd_batch_tokens_count(FL_SCMD_NETSTAT, ...) in cmd_batch_dispatch.c */
int cmd_netstat_batch_tokens_count(int argc, char **argv, int i);
/* Indirect: fl_shell_cmd_batch_tokens_count(FL_SCMD_NSLOOKUP, ...) in cmd_batch_dispatch.c */
int cmd_nslookup_batch_tokens_count(int argc, char **argv, int i);
/* Indirect: fl_shell_cmd_batch_tokens_count(FL_SCMD_NETSH, ...) in cmd_batch_dispatch.c */
int cmd_netsh_batch_tokens_count(int argc, char **argv, int i);
/* Indirect: fl_shell_cmd_batch_tokens_count(FL_SCMD_DHCP, ...) in cmd_batch_dispatch.c */
int cmd_dhcp_batch_tokens_count(int argc, char **argv, int i);

#endif /* CMD_DECL_H */
