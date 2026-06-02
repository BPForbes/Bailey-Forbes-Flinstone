#include "cmd_decl.h"

int fl_shell_cmd_batch_tokens_count(fl_shell_cmd_no_t no, int argc, char **argv, int i) {
    switch (no) {
    case FL_SCMD_CD:
        return cmd_cd_batch_tokens_count(argc, argv, i);
    case FL_SCMD_CREATEDISK:
        return cmd_createdisk_batch_tokens_count(argc, argv, i);
    case FL_SCMD_FORMAT:
        return cmd_format_batch_tokens_count(argc, argv, i);
    case FL_SCMD_SETDISK:
        return cmd_setdisk_batch_tokens_count(argc, argv, i);
    case FL_SCMD_VERSION:
        return cmd_version_batch_tokens_count(argc, argv, i);
    case FL_SCMD_CONTRACTS:
        return cmd_contracts_batch_tokens_count(argc, argv, i);
    case FL_SCMD_AUDIT:
        return cmd_audit_batch_tokens_count(argc, argv, i);
    case FL_SCMD_LISTCLUSTERS:
        return cmd_listclusters_batch_tokens_count(argc, argv, i);
    case FL_SCMD_LISTDIRS:
        return cmd_listdirs_batch_tokens_count(argc, argv, i);
    case FL_SCMD_SEARCH:
        return cmd_search_batch_tokens_count(argc, argv, i);
    case FL_SCMD_WRITECLUSTER:
        return cmd_writecluster_batch_tokens_count(argc, argv, i);
    case FL_SCMD_DELCLUSTER:
        return cmd_delcluster_batch_tokens_count(argc, argv, i);
    case FL_SCMD_UPDATE:
        return cmd_update_batch_tokens_count(argc, argv, i);
    case FL_SCMD_TYPE:
    case FL_SCMD_CAT:
        return cmd_cat_batch_tokens_count(argc, argv, i);
    case FL_SCMD_REDIRECT:
        return cmd_redirect_batch_tokens_count(argc, argv, i);
    case FL_SCMD_INITDISK:
        return cmd_initdisk_batch_tokens_count(argc, argv, i);
    case FL_SCMD_RERUN:
        return cmd_rerun_batch_tokens_count(argc, argv, i);
    case FL_SCMD_IMPORT:
        return cmd_import_batch_tokens_count(argc, argv, i);
    case FL_SCMD_DU:
        return cmd_du_batch_tokens_count(argc, argv, i);
    case FL_SCMD_PRINTDISK:
        return cmd_printdisk_batch_tokens_count(argc, argv, i);
    case FL_SCMD_DIR:
        return cmd_dir_batch_tokens_count(argc, argv, i);
    case FL_SCMD_MKDIR:
        return cmd_mkdir_batch_tokens_count(argc, argv, i);
    case FL_SCMD_RMDIR:
        return cmd_rmdir_batch_tokens_count(argc, argv, i);
    case FL_SCMD_RMTREE:
        return cmd_rmtree_batch_tokens_count(argc, argv, i);
    case FL_SCMD_WRITE:
        return cmd_write_batch_tokens_count(argc, argv, i);
    case FL_SCMD_MV:
        return cmd_mv_batch_tokens_count(argc, argv, i);
    case FL_SCMD_WHERE:
    case FL_SCMD_LOC:
        return cmd_where_batch_tokens_count(argc, argv, i);
    case FL_SCMD_ADDCLUSTER:
        return cmd_addcluster_batch_tokens_count(argc, argv, i);
    case FL_SCMD_DISKPUT:
        return cmd_diskput_batch_tokens_count(argc, argv, i);
    case FL_SCMD_DISKGET:
        return cmd_diskget_batch_tokens_count(argc, argv, i);
    case FL_SCMD_DISKFILES:
        return cmd_diskfiles_batch_tokens_count(argc, argv, i);
    case FL_SCMD_DISKDEL:
        return cmd_diskdel_batch_tokens_count(argc, argv, i);
    case FL_SCMD_DISKMKDIR:
        return cmd_diskmkdir_batch_tokens_count(argc, argv, i);
    case FL_SCMD_SUDO:
        return cmd_sudo_batch_tokens_count(argc, argv, i);
    case FL_SCMD_SU:
        return cmd_su_batch_tokens_count(argc, argv, i);
    case FL_SCMD_LOGIN:
        return cmd_login_batch_tokens_count(argc, argv, i);
    case FL_SCMD_LOGOUT:
        return cmd_logout_batch_tokens_count(argc, argv, i);
    case FL_SCMD_USERADD:
        return cmd_useradd_batch_tokens_count(argc, argv, i);
    case FL_SCMD_USERDEL:
        return cmd_userdel_batch_tokens_count(argc, argv, i);
    case FL_SCMD_PASSWD:
        return cmd_passwd_batch_tokens_count(argc, argv, i);
    case FL_SCMD_WHOAMI:
        return cmd_whoami_batch_tokens_count(argc, argv, i);
    case FL_SCMD_PING:
        return cmd_ping_batch_tokens_count(argc, argv, i);
    case FL_SCMD_PING6:
        return cmd_ping6_batch_tokens_count(argc, argv, i);
    case FL_SCMD_CHECK:
        return cmd_check_batch_tokens_count(argc, argv, i);
    case FL_SCMD_SERVER:
        return cmd_server_batch_tokens_count(argc, argv, i);
    case FL_SCMD_UDPSEND:
        return cmd_udpsend_batch_tokens_count(argc, argv, i);
    case FL_SCMD_UDPLISTEN:
        return cmd_udplisten_batch_tokens_count(argc, argv, i);
    case FL_SCMD_ARP:
        return cmd_arp_batch_tokens_count(argc, argv, i);
    case FL_SCMD_IFCONFIG:
        return cmd_ifconfig_batch_tokens_count(argc, argv, i);
    case FL_SCMD_ROUTE:
        return cmd_route_batch_tokens_count(argc, argv, i);
    case FL_SCMD_NETSTAT:
        return cmd_netstat_batch_tokens_count(argc, argv, i);
    case FL_SCMD_NSLOOKUP:
    case FL_SCMD_RESOLVE:
        return cmd_nslookup_batch_tokens_count(argc, argv, i);
    case FL_SCMD_NETSH:
        return cmd_netsh_batch_tokens_count(argc, argv, i);
    case FL_SCMD_WIFI:
        return cmd_wifi_batch_tokens_count(argc, argv, i);
    default:
        return 1;
    }
}
