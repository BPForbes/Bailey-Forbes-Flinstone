#ifndef FL_KQ_H
#define FL_KQ_H

/*
 * The monitor's entry points (FL-02): declarations only. Every body is delivered by the story named
 * beside it and calls fl_kq_driver_get() and the existing fl_kq_cp_* checkpoints; nothing here talks
 * to Rust directly, so no behaviour is decided twice.
 */

#include "contract_result.h"
#include "fl/driver/keyquorum.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Wire-visible to FL-03: append only, never renumber. */
typedef enum {
    FL_KQ_OP_CREATE = 1,
    FL_KQ_OP_WRITE,
    FL_KQ_OP_MKDIR,
    FL_KQ_OP_DELETE,
    FL_KQ_OP_MOVE,
    FL_KQ_OP_RECEIVE
} fl_kq_op_t;

fl_result_t fl_kq_pre_mutation(const char *path);                  /* body: FL-09 */
fl_result_t fl_kq_after_mutation(const char *path, fl_kq_op_t op); /* body: FL-09 (tier 1: sign as the current account) */
fl_result_t fl_kq_gate_receive(const kq_file_offer_t *o, const uint8_t *data, size_t len,
                               uint16_t receiver);                 /* body: FL-06 */
fl_result_t fl_kq_after_receive(const char *landed_path, const kq_file_offer_t *o); /* body: FL-06 */
void fl_kq_report_refusal(const kq_file_offer_t *o, fl_result_t why); /* body: FL-06, writes the KQER sidecar */
fl_result_t fl_kq_block_written(uint32_t lba);                     /* body: FL-09 */

/** body: FL-12. The rule lives in KeyQuorum (access, bridge proof); this only asks it. */
typedef struct {
    int factor;
    int bridge_index;
    int needs_parent_grant;
} fl_kq_requirement_t;
fl_kq_requirement_t fl_kq_unlock_requirement(const char *current_user, const char *target_user, int sudo);
int fl_kq_open_account(const char *user, const char *password, const char *kqst_path_or_null);

#ifdef __cplusplus
}
#endif

#endif /* FL_KQ_H */
