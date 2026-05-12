/**
 * Authorization **check** contract (**P0-1**, **P2-3**).
 *
 * `op` is a subsystem-defined unsigned code (netdev mount, raw I/O, etc.).
 * `ctx` is opaque to this header (principal, path, device handle, …).
 * Return `FL_AUTHZ_ALLOW` only when policy permits; log denies via **P6-4** when wired.
 */
#ifndef FL_CONTRACT_AUTH_H
#define FL_CONTRACT_AUTH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FL_AUTHZ_DENY = 0,
    FL_AUTHZ_ALLOW = 1
} fl_authz_decision_t;

typedef fl_authz_decision_t (*fl_authz_check_fn)(unsigned op, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* FL_CONTRACT_AUTH_H */
