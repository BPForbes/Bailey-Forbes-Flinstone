/**
 * **P5-5 — Server share staging** (module contract, normative).
 *
 * **Distribution:** accepted server-file offers land in **`server_shared`**
 * under the offered **`file_name`** (flat directory, no share id in the basename;
 * numeric **` (n)`** suffix when the name collides). In-flight wire sidecar
 * fields are not written to the payload file — host **`<landed>.meta`** records
 * **`share_id`** and optional **`expires_at`** for re-accept and purge into
 * **`server_shared/expired/`**. Offers can
 * written over a matching local destination when the overwrite bit permits it,
 * or be declined.
 */
#ifndef FL_CONTRACT_P5_SERVER_SHARE_H
#define FL_CONTRACT_P5_SERVER_SHARE_H

#include "contract_extend.h"

#include <stdint.h>

#define FL_CONTRACT_P5_5_SERVER_SHARE_CONTRACT_DEFINED 1

/** Default directory name (single path component; no trailing slash in the constant). */
#define FL_SERVER_SHARED_DIR_NAME "server_shared"
#define FL_SERVER_SHARED_EXPIRED_DIR_NAME "expired"
/** Legacy alias retained for older docs and SFTP virtual paths. */
#define FL_SERVER_SHARE_DIR_NAME FL_SERVER_SHARED_DIR_NAME
#define FL_SERVER_SHARE_INBOX_DIR_NAME "inbox"
#define FL_SERVER_SHARE_PUBLIC_DIR_NAME "public"
#define FL_SERVER_SHARE_SENT_DIR_NAME "sent"

/** Max bytes for a normalized relative path under the share tree (including NUL budget). */
#ifndef FL_SERVER_SHARE_REL_PATH_MAX
#define FL_SERVER_SHARE_REL_PATH_MAX 4096u
#endif

/** Max bytes stored for the sender's captured absolute/resolved path on the wire. */
#ifndef FL_SERVER_SHARE_SENDER_PATH_MAX
#define FL_SERVER_SHARE_SENDER_PATH_MAX FL_SERVER_SHARE_REL_PATH_MAX
#endif

struct fl_server_file_offer;

/** Receiver choice after a FILE_OFFER arrives. */
typedef enum fl_server_file_disposition {
    FL_SERVER_FILE_DECLINE = 0,
    FL_SERVER_FILE_SAVE_TO_SERVER_SHARE = 1,
    FL_SERVER_FILE_OVERWRITE_LOCAL = 2
} fl_server_file_disposition_t;

fl_result_t fl_server_share_inbox_list(uint16_t member_id);
fl_result_t fl_server_share_public_list(void);
fl_result_t fl_server_share_sent_list(uint16_t member_id);

fl_result_t fl_server_share_accept(const char *share_id,
                                   uint16_t receiver_id,
                                   fl_server_file_disposition_t disposition);

fl_result_t fl_server_share_decline(const char *share_id,
                                    uint16_t receiver_id);

fl_result_t fl_server_share_revoke(const char *share_id,
                                   uint16_t owner_id);

fl_result_t fl_server_share_save_to_server_share(const struct fl_server_file_offer *offer);

fl_result_t fl_server_share_overwrite_local(const struct fl_server_file_offer *offer,
                                            const char *local_path);

_Static_assert(FL_SERVER_SHARE_REL_PATH_MAX >= 256u,
               "server_share path cap must cover normal user trees");

#endif /* FL_CONTRACT_P5_SERVER_SHARE_H */
