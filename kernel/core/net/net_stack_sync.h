#ifndef NET_STACK_SYNC_H
#define NET_STACK_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

/** Hosted builds: serialize in-tree TCP/loopback/netdev RX (server BG + foreground). */
void fl_net_stack_sync_init(void);
void fl_net_stack_lock(void);
void fl_net_stack_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_STACK_SYNC_H */
