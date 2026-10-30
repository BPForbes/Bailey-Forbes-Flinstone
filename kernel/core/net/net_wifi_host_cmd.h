#ifndef FL_NET_WIFI_HOST_CMD_H
#define FL_NET_WIFI_HOST_CMD_H

/*
 * Running the host's own Wi-Fi tools (wpa_cli, nmcli, iw, the Flinstone helper) goes through the
 * host's /bin/sh. The Wi-Fi host layer (net_wifi_host_linux.c, net_wifi_he.c) therefore starts a
 * command only through these three, and they do nothing unless the build asked for it
 * (make WIFI_HOST_COMMANDS=1, which defines FL_WIFI_HOST_COMMANDS). A default build has no popen
 * or system call in it and uses the in-tree Wi-Fi drivers only.
 *
 * Private to those two files: include nothing else from here, and add no other command start.
 */

#include <stdio.h>
#include <stdlib.h>

#if defined(FL_WIFI_HOST_COMMANDS)
static inline FILE *fl_host_popen(const char *cmd, const char *mode) { return popen(cmd, mode); }
static inline int fl_host_pclose(FILE *fp) { return pclose(fp); }
static inline int fl_host_system(const char *cmd) { return system(cmd); }
#else
static inline FILE *fl_host_popen(const char *cmd, const char *mode) { (void)cmd; (void)mode; return NULL; }
static inline int fl_host_pclose(FILE *fp) { (void)fp; return -1; }
static inline int fl_host_system(const char *cmd) { (void)cmd; return -1; }
#endif

#endif
