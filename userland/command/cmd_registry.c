#include "fl_shell_cmd.h"
#include <string.h>

typedef struct {
    const char *name;
    fl_shell_cmd_no_t id;
} cmd_name_ent_t;

static const cmd_name_ent_t cmd_registry[] = {
    {"addcluster", FL_SCMD_ADDCLUSTER},
    {"arp", FL_SCMD_ARP},
    {"audit", FL_SCMD_AUDIT},
    {"cat", FL_SCMD_CAT},
    {"check", FL_SCMD_CHECK},
    {"cd", FL_SCMD_CD},
    {"contracts", FL_SCMD_CONTRACTS},
    {"createdisk", FL_SCMD_CREATEDISK},
    {"dhcp", FL_SCMD_DHCP},
    {"delcluster", FL_SCMD_DELCLUSTER},
    {"diskdel", FL_SCMD_DISKDEL},
    {"diskfiles", FL_SCMD_DISKFILES},
    {"diskget", FL_SCMD_DISKGET},
    {"diskmkdir", FL_SCMD_DISKMKDIR},
    {"diskput", FL_SCMD_DISKPUT},
    {"dir", FL_SCMD_DIR},
    {"du", FL_SCMD_DU},
    {"format", FL_SCMD_FORMAT},
    {"ifconfig", FL_SCMD_IFCONFIG},
    {"import", FL_SCMD_IMPORT},
    {"initdisk", FL_SCMD_INITDISK},
    {"listclusters", FL_SCMD_LISTCLUSTERS},
    {"listdirs", FL_SCMD_LISTDIRS},
    {"login", FL_SCMD_LOGIN},
    {"logout", FL_SCMD_LOGOUT},
    {"loc", FL_SCMD_LOC},
    {"mkdir", FL_SCMD_MKDIR},
    {"mv", FL_SCMD_MV},
    {"netsh", FL_SCMD_NETSH},
    {"wifi", FL_SCMD_WIFI},
    {"netstat", FL_SCMD_NETSTAT},
    {"nslookup", FL_SCMD_NSLOOKUP},
    {"resolve", FL_SCMD_RESOLVE},
    {"printdisk", FL_SCMD_PRINTDISK},
    {"redirect", FL_SCMD_REDIRECT},
    {"rerun", FL_SCMD_RERUN},
    {"rmdir", FL_SCMD_RMDIR},
    {"rmtree", FL_SCMD_RMTREE},
    {"route", FL_SCMD_ROUTE},
    {"search", FL_SCMD_SEARCH},
    {"server", FL_SCMD_SERVER},
    {"setdisk", FL_SCMD_SETDISK},
    {"sudo", FL_SCMD_SUDO},
    {"su", FL_SCMD_SU},
    {"useradd", FL_SCMD_USERADD},
    {"userdel", FL_SCMD_USERDEL},
    {"passwd", FL_SCMD_PASSWD},
    {"ping", FL_SCMD_PING},
    {"ping6", FL_SCMD_PING6},
    {"whoami", FL_SCMD_WHOAMI},
    {"type", FL_SCMD_TYPE},
    {"udplisten", FL_SCMD_UDPLISTEN},
    {"udpsend", FL_SCMD_UDPSEND},
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
