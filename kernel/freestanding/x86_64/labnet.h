#ifndef FL_FREESTANDING_LABNET_H
#define FL_FREESTANDING_LABNET_H

void fl_fs_labnet_init(void);
void fl_fs_labnet_ifconfig(void (*emit)(const char *), void (*emit_uint)(unsigned));
void fl_fs_labnet_arp(const char *op, const char *ip, const char *mac,
                      void (*emit)(const char *), void (*emit_uint)(unsigned));
void fl_fs_labnet_route(void (*emit)(const char *));
void fl_fs_labnet_netstat(void (*emit)(const char *), void (*emit_uint)(unsigned));
int fl_fs_labnet_resolve(const char *host, char *v4, unsigned v4cap, char *v6, unsigned v6cap);
int fl_fs_labnet_nslookup(const char *host, void (*emit)(const char *));
int fl_fs_labnet_ping(const char *host, unsigned port, int v6,
                      void (*emit)(const char *), void (*emit_uint)(unsigned));
int fl_fs_labnet_check(const char *host, unsigned port,
                       void (*emit)(const char *), void (*emit_uint)(unsigned));
int fl_fs_labnet_dnsack(const char *host, const char *v4, const char *v6,
                        void (*emit)(const char *), void (*emit_uint)(unsigned));
int fl_fs_labnet_udpsend(const char *endpoint, const char *msg);
int fl_fs_labnet_udplisten(unsigned port, char *out, unsigned cap);
void fl_fs_labnet_wifi(const char *op, const char *arg,
                       void (*emit)(const char *));

#endif
