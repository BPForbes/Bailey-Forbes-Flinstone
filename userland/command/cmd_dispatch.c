#include "cmd_decl.h"
#include <stddef.h>

/*
 * Numeric dispatch — same structure as fl_syscall_dispatch(): switch on command id.
 */
int fl_shell_cmd_dispatch(fl_shell_cmd_no_t no, int argc, char **argv) {
    switch (no) {
    case FL_SCMD_CD:
        return cmd_cd_run(argc, argv);
    case FL_SCMD_CREATEDISK:
        return cmd_createdisk_run(argc, argv);
    case FL_SCMD_FORMAT:
        return cmd_format_run(argc, argv);
    case FL_SCMD_SETDISK:
        return cmd_setdisk_run(argc, argv);
    case FL_SCMD_VERSION:
        return cmd_version_run(argc, argv);
    case FL_SCMD_CONTRACTS:
        return cmd_contracts_run(argc, argv);
    case FL_SCMD_LISTCLUSTERS:
        return cmd_listclusters_run(argc, argv);
    case FL_SCMD_LISTDIRS:
        return cmd_listdirs_run(argc, argv);
    case FL_SCMD_SEARCH:
        return cmd_search_run(argc, argv);
    case FL_SCMD_WRITECLUSTER:
        return cmd_writecluster_run(argc, argv);
    case FL_SCMD_DELCLUSTER:
        return cmd_delcluster_run(argc, argv);
    case FL_SCMD_UPDATE:
        return cmd_update_run(argc, argv);
    case FL_SCMD_TYPE:
    case FL_SCMD_CAT:
        return cmd_cat_run(argc, argv);
    case FL_SCMD_REDIRECT:
        return cmd_redirect_run(argc, argv);
    case FL_SCMD_INITDISK:
        return cmd_initdisk_run(argc, argv);
    case FL_SCMD_RERUN:
        return cmd_rerun_run(argc, argv);
    case FL_SCMD_IMPORT:
        return cmd_import_run(argc, argv);
    case FL_SCMD_DU:
        return cmd_du_run(argc, argv);
    case FL_SCMD_PRINTDISK:
        return cmd_printdisk_run(argc, argv);
    case FL_SCMD_DIR:
        return cmd_dir_run(argc, argv);
    case FL_SCMD_MKDIR:
        return cmd_mkdir_run(argc, argv);
    case FL_SCMD_RMDIR:
        return cmd_rmdir_run(argc, argv);
    case FL_SCMD_RMTREE:
        return cmd_rmtree_run(argc, argv);
    case FL_SCMD_WRITE:
        return cmd_write_run(argc, argv);
    case FL_SCMD_MV:
        return cmd_mv_run(argc, argv);
    case FL_SCMD_WHERE:
    case FL_SCMD_LOC:
        return cmd_where_run(argc, argv);
    case FL_SCMD_ADDCLUSTER:
        return cmd_addcluster_run(argc, argv);
    case FL_SCMD_DISKPUT:
        return cmd_diskput_run(argc, argv);
    case FL_SCMD_DISKGET:
        return cmd_diskget_run(argc, argv);
    case FL_SCMD_DISKFILES:
        return cmd_diskfiles_run(argc, argv);
    case FL_SCMD_DISKDEL:
        return cmd_diskdel_run(argc, argv);
    case FL_SCMD_DISKMKDIR:
        return cmd_diskmkdir_run(argc, argv);
    default:
        return -1;
    }
}
