#ifndef CMD_SERVER_HISTORY_H
#define CMD_SERVER_HISTORY_H
/*
 * Member side of `server file history <delivery-id> [-from <account-key>]`: remembered deliveries,
 * argument and sender resolution, and the fetch. Printing stays in cmd_server_file.c.
 */
#include "contract_result.h"
#include "fl/driver/keyquorum.h"
#include "kq_delivery.h"
#include "net_kq_delivery.h"
#include "user_db.h"

#include <pthread.h>
#include <stdint.h>

#define FL_SERVER_HISTORY_GOT_MAX 16u

/* One delivery this shell decoded. */
typedef struct {
    int used;
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    char sender[FL_HERITAGE_KEY_ID_MAX];
    char file_name[128];
    uint8_t file_id[16];
    int has_file_id; /* 0 on the -from path: the history cannot be tied to a decoded delivery */
} fl_server_history_got_t;

/** Remembers a decoded delivery (newest 16; the same id replaces its record). Over-long strings, an
 *  empty sender or a NULL file_id are not remembered. Thread-safe. */
void fl_server_history_remember(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], const char *sender, const char *file_name,
                                const uint8_t file_id[16]);
/** Copies the record for id to out. Returns 0 when this shell has not decoded it. Thread-safe. */
int fl_server_history_find(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], fl_server_history_got_t *out);
/** Forgets every record (tests). */
void fl_server_history_forget_all(void);

typedef enum {
    FL_SERVER_HISTORY_ARGS_OK = 0,
    FL_SERVER_HISTORY_ARGS_USAGE,       /* no delivery id, or an unexpected argument (*bad names it) */
    FL_SERVER_HISTORY_ARGS_NOT_DELIVERY, /* a share id: only the host keeps that history */
    FL_SERVER_HISTORY_ARGS_FROM_MISMATCH, /* -from names someone other than the delivery's remembered sender */
    FL_SERVER_HISTORY_ARGS_NO_SENDER,   /* not decoded here and no -from */
    FL_SERVER_HISTORY_ARGS_BAD_FROM     /* -from empty or too long for an account key */
} fl_server_history_args_t;

/** Parses the verb's arguments (argv[0] is "history"). On OK *out holds the id and sender and *hex the
 *  id as typed; on USAGE *bad points at the offending argument. */
fl_server_history_args_t fl_server_history_args(int argc, char **argv, fl_server_history_got_t *out,
                                                const char **hex, const char **bad);

/** Returns 1 when the host registry holds sender under that exact key id (no name resolution). */
int fl_server_history_sender_matches(const fl_net_kq_keys_t *keys, const char *sender);

/** Sends the request under mutex and opens the answer with box (fl_net_kq_history_open). A request
 *  that times out is dropped. BUSY or a send error is returned unchanged. */
fl_result_t fl_server_history_fetch(pthread_mutex_t *mutex, fl_net_server_t *srv, fl_net_client_t *client,
                                    const fl_server_history_got_t *got, const unsigned char box[32],
                                    const char *me, const uint8_t sender_public[32], unsigned wait_ms,
                                    fl_kq_buf_t *lines, uint32_t *events);

#endif /* CMD_SERVER_HISTORY_H */
