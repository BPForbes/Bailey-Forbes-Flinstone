#include "fl_shell_cmd.h"
#include <string.h>

typedef struct {
    const char *name;
    fl_shell_cmd_no_t id;
} cmd_name_ent_t;

static const cmd_name_ent_t cmd_registry[] = {
    {"addcluster", FL_SCMD_ADDCLUSTER},
    {"cat", FL_SCMD_CAT},
    {"cd", FL_SCMD_CD},
    {"createdisk", FL_SCMD_CREATEDISK},
    {"delcluster", FL_SCMD_DELCLUSTER},
    {"diskdel", FL_SCMD_DISKDEL},
    {"diskfiles", FL_SCMD_DISKFILES},
    {"diskget", FL_SCMD_DISKGET},
    {"diskput", FL_SCMD_DISKPUT},
    {"dir", FL_SCMD_DIR},
    {"du", FL_SCMD_DU},
    {"format", FL_SCMD_FORMAT},
    {"import", FL_SCMD_IMPORT},
    {"initdisk", FL_SCMD_INITDISK},
    {"listclusters", FL_SCMD_LISTCLUSTERS},
    {"listdirs", FL_SCMD_LISTDIRS},
    {"loc", FL_SCMD_LOC},
    {"mkdir", FL_SCMD_MKDIR},
    {"mv", FL_SCMD_MV},
    {"printdisk", FL_SCMD_PRINTDISK},
    {"redirect", FL_SCMD_REDIRECT},
    {"rerun", FL_SCMD_RERUN},
    {"rmdir", FL_SCMD_RMDIR},
    {"rmtree", FL_SCMD_RMTREE},
    {"search", FL_SCMD_SEARCH},
    {"setdisk", FL_SCMD_SETDISK},
    {"type", FL_SCMD_TYPE},
    {"update", FL_SCMD_UPDATE},
    {"version", FL_SCMD_VERSION},
    {"where", FL_SCMD_WHERE},
    {"write", FL_SCMD_WRITE},
    {"writecluster", FL_SCMD_WRITECLUSTER},
};

fl_shell_cmd_no_t fl_shell_cmd_lookup(const char *name) {
    if (!name || !name[0])
        return FL_SCMD_UNKNOWN;
    for (unsigned i = 0; i < sizeof(cmd_registry) / sizeof(cmd_registry[0]); i++) {
        if (!strcmp(name, cmd_registry[i].name))
            return cmd_registry[i].id;
    }
    return FL_SCMD_UNKNOWN;
}
