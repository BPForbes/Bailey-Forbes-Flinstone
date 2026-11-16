/* Member side of `server file history <delivery-id>`: see cmd_server_history.h. */
#include "cmd_server_history.h"

#include <stdio.h>
#include <string.h>

/* For the life of this shell process (nothing clears it when a session ends; a record only names whose letters to
 * expect, and the host's gate and this account's box key still decide). The newest FL_SERVER_HISTORY_GOT_MAX.
 * Readers copy a record under the lock and never keep a pointer into the ring. */
static fl_server_history_got_t s_got[FL_SERVER_HISTORY_GOT_MAX];
static unsigned s_got_next;
static pthread_mutex_t s_got_mu = PTHREAD_MUTEX_INITIALIZER;

/* Caller holds s_got_mu: the slot holding id, or FL_SERVER_HISTORY_GOT_MAX. */
static unsigned got_slot_locked(const uint8_t id[FL_KQ_DELIVERY_ID_LEN])
{
    unsigned i;

    for (i = 0; i < FL_SERVER_HISTORY_GOT_MAX; i++)
        if (s_got[i].used && memcmp(s_got[i].id, id, FL_KQ_DELIVERY_ID_LEN) == 0)
            break;
    return i;
}

void fl_server_history_remember(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], const char *sender, const char *file_name,
                                const uint8_t file_id[16])
{
    fl_server_history_got_t fresh;
    unsigned slot;

    memset(&fresh, 0, sizeof fresh);
    if (!id || !sender || !file_name || !file_id || !sender[0] || strlen(sender) >= sizeof fresh.sender ||
        strlen(file_name) >= sizeof fresh.file_name)
        return; /* not remembered: `server file history` then needs -from */
    memcpy(fresh.id, id, FL_KQ_DELIVERY_ID_LEN);
    snprintf(fresh.sender, sizeof fresh.sender, "%s", sender);
    snprintf(fresh.file_name, sizeof fresh.file_name, "%s", file_name);
    memcpy(fresh.file_id, file_id, sizeof fresh.file_id);
    fresh.has_file_id = 1;
    fresh.used = 1;
    pthread_mutex_lock(&s_got_mu);
    /* A delivery decoded again replaces its own record; anything else takes the oldest slot. */
    slot = got_slot_locked(id);
    if (slot == FL_SERVER_HISTORY_GOT_MAX) {
        slot = s_got_next;
        s_got_next = (s_got_next + 1u) % FL_SERVER_HISTORY_GOT_MAX;
    }
    s_got[slot] = fresh;
    pthread_mutex_unlock(&s_got_mu);
}

int fl_server_history_find(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], fl_server_history_got_t *out)
{
    unsigned slot;

    if (!id || !out)
        return 0;
    pthread_mutex_lock(&s_got_mu);
    slot = got_slot_locked(id);
    if (slot < FL_SERVER_HISTORY_GOT_MAX)
        *out = s_got[slot];
    pthread_mutex_unlock(&s_got_mu);
    return slot < FL_SERVER_HISTORY_GOT_MAX;
}

void fl_server_history_forget_all(void)
{
    pthread_mutex_lock(&s_got_mu);
    memset(s_got, 0, sizeof s_got);
    s_got_next = 0;
    pthread_mutex_unlock(&s_got_mu);
}

fl_server_history_args_t fl_server_history_args(int argc, char **argv, fl_server_history_got_t *out,
                                                const char **hex, const char **bad)
{
    const char *from = NULL;
    const char *h = NULL;
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    int i;

    *hex = NULL;
    *bad = NULL;
    memset(out, 0, sizeof *out);
    i = 1;
    while (i < argc) {
        if (!strcmp(argv[i], "-from") && i + 1 < argc && !from) {
            from = argv[i + 1];
            i += 2;
        } else if (argv[i][0] != '-' && !h) {
            h = argv[i];
            i += 1;
        } else {
            *bad = argv[i];
            return FL_SERVER_HISTORY_ARGS_USAGE;
        }
    }
    if (!h)
        return FL_SERVER_HISTORY_ARGS_USAGE;
    *hex = h;
    if (fl_kq_delivery_id_parse(h, id) != 0)
        return FL_SERVER_HISTORY_ARGS_NOT_DELIVERY;
    if (from && (!from[0] || strlen(from) >= sizeof out->sender))
        return FL_SERVER_HISTORY_ARGS_BAD_FROM;
    if (fl_server_history_find(id, out))
        return from && strcmp(from, out->sender) != 0 ? FL_SERVER_HISTORY_ARGS_FROM_MISMATCH
                                                      : FL_SERVER_HISTORY_ARGS_OK;
    if (!from)
        return FL_SERVER_HISTORY_ARGS_NO_SENDER;
    memcpy(out->id, id, sizeof out->id);
    snprintf(out->sender, sizeof out->sender, "%s", from);
    snprintf(out->file_name, sizeof out->file_name, "%s", h);
    out->has_file_id = 0;
    out->used = 1;
    return FL_SERVER_HISTORY_ARGS_OK;
}

int fl_server_history_sender_matches(const fl_net_kq_keys_t *keys, const char *sender)
{
    return keys && sender && sender[0] && keys->found && strcmp(keys->key_id, sender) == 0;
}

fl_result_t fl_server_history_fetch(pthread_mutex_t *mutex, fl_net_server_t *srv, fl_net_client_t *client,
                                    const fl_server_history_got_t *got, const unsigned char box[32],
                                    const char *me, const uint8_t sender_public[32], unsigned wait_ms,
                                    fl_kq_buf_t *lines, uint32_t *events)
{
    uint8_t req[16];
    fl_result_t rc;

    if (!mutex || !got || !box || !me || !sender_public || !lines || !events)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(mutex);
    rc = fl_net_kq_request_history(srv, client, 0, got->id, req);
    pthread_mutex_unlock(mutex);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = fl_net_kq_history_open(req, wait_ms, box, got->sender, me, sender_public,
                                got->has_file_id ? got->file_id : NULL, lines, events);
    if (rc == FL_RESULT_TIMEDOUT)
        fl_net_kq_history_drop(req); /* given up: free the slot (an answer arriving later is ignored) */
    return rc;
}
