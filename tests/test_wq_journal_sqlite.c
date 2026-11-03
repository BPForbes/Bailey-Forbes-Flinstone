/* The workqueue's flushed journal (FL-25) on SQLite: the same scenarios as the memory journal, with a
 * real restart (close and reopen the file) in between. Hosted only. */
#include "wq_journal_sqlite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static unsigned s_ran;
static fl_result_t handler(const void *p, size_t n, const uint8_t idem[16]) {
    (void)p; (void)n; (void)idem;
    s_ran++;
    return FL_RESULT_OK;
}

#define TAG_AUTO 0x7101u
#define TAG_ASK 0x7102u

static int test_survives_a_restart(const char *path) {
    fl_wq_journal_ops_t j;
    fl_workqueue_storage_t sa, sb;
    fl_workqueue_t *a = (fl_workqueue_t *)&sa, *b = (fl_workqueue_t *)&sb;
    uint8_t k1[16] = {1}, k2[16] = {2}, k3[16] = {3}, k4[16] = {4};
    uint8_t pl[3] = {7, 8, 9};

    s_ran = 0;
    ASSERT(fl_wq_register_handler(TAG_AUTO, handler, FL_WQ_RECOVER_AUTO) == FL_RESULT_OK);
    ASSERT(fl_wq_register_handler(TAG_ASK, handler, FL_WQ_RECOVER_ASK) == FL_RESULT_OK);
    ASSERT(fl_wq_journal_sqlite_open(path, &j) == FL_RESULT_OK);
    ASSERT(fl_wq_set_journal(&j) == fl_wq_journal_mem());
    ASSERT(fl_wq_init(a) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(a, TAG_AUTO, FL_WQ_LAYER_NORMAL, "Root.Bob", pl, sizeof pl, k1, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(a, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, k2, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(a, TAG_ASK, FL_WQ_LAYER_BACKGROUND, "Root.Bob", NULL, 0, k3, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(a, TAG_AUTO, FL_WQ_LAYER_NORMAL, "Root.Bob", pl, sizeof pl, k1, 0) == FL_RESULT_OK); /* repeat */
    ASSERT(j.count(j.ctx, FL_WQ_ST_QUEUED, NULL) == 3);
    fl_wq_shutdown(a); /* crash: memory gone */
    fl_wq_journal_sqlite_close(&j);

    ASSERT(fl_wq_journal_sqlite_open(path, &j) == FL_RESULT_OK); /* reboot: the file is all that is left */
    (void)fl_wq_set_journal(&j);
    ASSERT(j.count(j.ctx, FL_WQ_ST_QUEUED, NULL) == 3 && s_ran == 0);
    ASSERT(fl_wq_init(b) == FL_RESULT_OK);
    ASSERT(fl_wq_recover(b) == FL_RESULT_OK);
    ASSERT(fl_wq_held_count("Root.Bob") == 1); /* the user-visible one waits */
    ASSERT(fl_wq_poll_at(b, 8, 0) == 2 && s_ran == 2);
    ASSERT(j.count(j.ctx, FL_WQ_ST_DONE, NULL) == 2);
    {
        fl_wq_journal_record_t r;
        ASSERT(j.load(j.ctx, 1, &r) == FL_RESULT_OK);
        ASSERT(r.payload_len == 3 && r.payload[2] == 9 && strcmp(r.owner, "Root.Bob") == 0 && r.attempts == 1);
    }
    /* a stale compare-and-set loses cleanly */
    ASSERT(j.mark(j.ctx, 1, FL_WQ_ST_PENDING, FL_WQ_ST_QUEUED) == FL_RESULT_BUSY);
    /* declining the held occurrence drops only it; a later job with a new key still runs */
    ASSERT(fl_wq_resolve_held("Root.Bob", 0) == FL_RESULT_OK);
    ASSERT(j.count(j.ctx, FL_WQ_ST_SKIPPED, NULL) == 1);
    ASSERT(fl_wq_enqueue_durable(b, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, k4, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(b, 8, 0) == 1 && s_ran == 3);
    fl_wq_shutdown(b);
    (void)fl_wq_set_journal(NULL);
    fl_wq_journal_sqlite_close(&j);
    return 0;
}

int main(void) {
    char path[] = "/tmp/fl_wq_journal_XXXXXX";
    int fd = mkstemp(path);
    int rc;

    if (fd < 0)
        return 1;
    close(fd);
    rc = test_survives_a_restart(path);
    unlink(path);
    {
        char side[64];
        snprintf(side, sizeof side, "%s-wal", path); unlink(side);
        snprintf(side, sizeof side, "%s-shm", path); unlink(side);
    }
    if (rc)
        return 1;
    printf("test_wq_journal_sqlite: all passed\n");
    return 0;
}
