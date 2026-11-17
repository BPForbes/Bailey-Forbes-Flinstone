#ifndef FL_KQ_SERVICE_H
#define FL_KQ_SERVICE_H


/* Holds only signing secrets of signed-in accounts, wiped on logout and shutdown. */
/* When the service is not alive every monitored change is refused. */
#include "contract_result.h"
#include "fl/driver/keyquorum.h"
#include "kq_storage.h"
#include "user_db.h"
#include "workqueue.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    KQ_SIGNING_OWN = 1,       /* the author is signed in and signs with their own key */
    KQ_SIGNING_ASSEMBLED = 2  /* a parent assembled from its children's shares (not available yet) */
} kq_signing_mode_t;

#define FL_KQ_SERVICE_TICK_MS 100u
#define FL_KQ_HEARTBEAT_MAX_NS (2000ll * 1000000ll)
#define FL_KQ_AUTOSIGN_MAX 16u

fl_result_t fl_kq_service_start(void);
void fl_kq_service_shutdown(void);

fl_result_t fl_kq_service_online_for_session(const fl_user_record_t *account, const char *password,
                                             uint64_t login_generation);
fl_result_t fl_kq_service_online_from_login(const char *account, const char *password);
fl_result_t fl_kq_service_online_opened(const fl_user_record_t *account, const uint8_t secret[32],
                                        uint64_t login_generation);
fl_result_t fl_kq_service_online_box(const fl_user_record_t *account, const uint8_t box_secret[32],
                                     uint64_t login_generation);
fl_result_t fl_kq_obtain_box_key(const char *author_key_id, uint8_t out[32]);
void fl_kq_service_offline(const char *account);
size_t fl_kq_service_online_count(void);

/* Copy for the caller to wipe; ACCES when that account is not signed in. */
fl_result_t fl_kq_obtain_author_key(const char *author_key_id, uint8_t out[32], kq_signing_mode_t *mode);

int fl_kq_service_alive(void);
int fl_kq_service_keys_locked(void);
void fl_kq_service_report_stalled(void);

fl_result_t fl_kq_service_submit(const fl_wq_work_t *work);
fl_result_t fl_kq_service_submit_every(const fl_wq_work_t *work, int64_t period_ns);

typedef struct {
    const char *label;                                       /* audit subject; copied */
    void *ctx;
    int64_t (*last_signature_sec)(void *ctx);                /* wall seconds of the newest signature */
    fl_result_t (*head_author)(void *ctx, char *out, size_t cap); /* FL_RESULT_BUSY: forked, no head */
    fl_result_t (*sign)(void *ctx, const char *author, const uint8_t key[32], kq_signing_mode_t mode);
    int64_t interval_sec;
} fl_kq_autosign_t;
fl_result_t fl_kq_service_autosign_add(const fl_kq_autosign_t *spec);

typedef struct {
    const char *path;
} fl_kq_autosign_file_t;
fl_result_t fl_kq_autosign_head_author(void *ctx, char *out, size_t cap);

int64_t fl_kq_autosign_last_signature(void *ctx);
fl_result_t fl_kq_autosign_sign(void *ctx, const char *author, const uint8_t key[32], kq_signing_mode_t mode);

fl_result_t fl_kq_service_autosign_track_file(const char *path, int64_t interval_sec);

fl_result_t fl_kq_service_autosign_remove(const char *label);

#define FL_KQ_AUTOSIGN_DEFAULT_SEC 86400ll

void fl_kq_service_utc_now(char *out, size_t cap);

void fl_kq_report_refusal_in(const kq_file_offer_t *o, fl_result_t why, const fl_kq_dir_t *dir);

#include <pthread.h>
pthread_t fl_kq_service_thread_for_test(void);

#endif /* FL_KQ_SERVICE_H */
