/**
 * P3-13a — Socket interchange (normative; P3-13 application layer is separate).
 *
 * Distribution: integer fl_net_sock_handle_t maps to one L4 channel. On hosted (H),
 * the implementation may delegate to POSIX sockets when no in-tree route exists;
 * on K/B, the same API surface routes through P3-6 UDP and P3-7 TCP once wired.
 *
 * Not in this shard: chat relay, file transfer, or shell server subcommands
 * (see docs/SERVER.md and a future contract_p3_server_wire.h).
 */
#ifndef FL_CONTRACT_P3_SOCKETS_H
#define FL_CONTRACT_P3_SOCKETS_H

#include "contract_p3_socket.h"
#include "contract_p3_tcp.h"
#include "contract_p3_udp.h"

#define FL_CONTRACT_P3_13A_SOCKETS_CONTRACT_DEFINED 1

/** Opaque positive handle; 0 is invalid. */
typedef int fl_net_sock_handle_t;

#define FL_NET_SOCK_INVALID ((fl_net_sock_handle_t)0)

typedef enum {
    FL_NET_SOCK_TYPE_STREAM = 1,
    FL_NET_SOCK_TYPE_DGRAM = 2
} fl_net_sock_type_t;

/** Max simultaneous handles in lab builds (hosted table or in-tree PCB table). */
#ifndef FL_NET_SOCK_TABLE_MAX
#define FL_NET_SOCK_TABLE_MAX 32
#endif

/** Default listen backlog for P3-13 server prep (RFC 793 shaped). */
#ifndef FL_NET_SOCK_DEFAULT_LISTEN_BACKLOG
#define FL_NET_SOCK_DEFAULT_LISTEN_BACKLOG 8
#endif

/** Max single send or recv buffer per call (stream or datagram payload). */
#ifndef FL_NET_SOCK_IO_CHUNK_MAX
#define FL_NET_SOCK_IO_CHUNK_MAX 65536u
#endif

_Static_assert(FL_NET_SOCK_TABLE_MAX >= 4u, "socket table must allow a minimal lab session");
_Static_assert(FL_NET_SOCK_DEFAULT_LISTEN_BACKLOG >= 1u, "listen backlog must be positive");

#endif /* FL_CONTRACT_P3_SOCKETS_H */
