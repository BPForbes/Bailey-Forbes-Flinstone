/**
 * test_kq_delivery.c - the host's co-signed delivery store (kernel/core/vfs/kq_delivery.c):
 * integrate-and-fire at FL_KQ_HOST_THRESHOLD, fire once, leak on the server clock, and the
 * SQLite store with its held letters (contracts/drivers/contract_p4_keyquorum.h).
 */
#include "kq_delivery.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

static char s_dir[PATH_MAX];

static void rmrf(const char *path) {
    DIR *d = opendir(path);
    struct dirent *e;
    if (!d) {
        unlink(path);
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char sub[PATH_MAX];
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
        rmrf(sub);
    }
    closedir(d);
    rmdir(path);
}

static void make_id(uint8_t id[FL_KQ_DELIVERY_ID_LEN], uint8_t seed) {
    for (size_t i = 0; i < FL_KQ_DELIVERY_ID_LEN; i++)
        id[i] = (uint8_t)(seed + i);
}

static int test_neuron_fires_once_at_threshold(void) {
    fl_kq_delivery_t d;
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint8_t sig[64];

    make_id(id, 1);
    memset(sig, 0xA5, sizeof sig);
    fl_kq_delivery_init(&d, id, "Root.Flinstone.Mya", "Root.Flinstone.Steve", "plan.txt", NULL,
                        1000, 60);
    ASSERT(d.potential == 1u && d.threshold == FL_KQ_HOST_THRESHOLD);
    /* Announcing is not a signature. */
    ASSERT(fl_kq_delivery_integrate(&d, FL_KQ_SPIKE_ANNOUNCE, NULL, 1001) == FL_KQ_NEURON_REST);
    ASSERT(d.potential == 1u);
    ASSERT(fl_kq_delivery_integrate(&d, FL_KQ_SPIKE_COSIGN, sig, 1002) == FL_KQ_NEURON_FIRE);
    ASSERT(d.fired && d.has_cosig && d.potential == 2u);
    /* Refractory: a second co-signature never releases again. */
    ASSERT(fl_kq_delivery_integrate(&d, FL_KQ_SPIKE_COSIGN, sig, 1003) == FL_KQ_NEURON_REFRACTORY);
    ASSERT(d.potential == 2u);
    return 0;
}

static int test_neuron_leaks_after_expiry(void) {
    fl_kq_delivery_t d;
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint8_t sig[64];

    make_id(id, 2);
    memset(sig, 0x5A, sizeof sig);
    fl_kq_delivery_init(&d, id, "a", "b", "f", NULL, 1000, 60);
    ASSERT(fl_kq_delivery_integrate(&d, FL_KQ_SPIKE_COSIGN, sig, 1060) == FL_KQ_NEURON_LEAKED);
    ASSERT(!d.fired && !d.has_cosig && d.potential == 1u);
    return 0;
}

static int test_ids_round_trip(void) {
    uint8_t id[FL_KQ_DELIVERY_ID_LEN], back[FL_KQ_DELIVERY_ID_LEN];
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];

    make_id(id, 0xF0);
    fl_kq_delivery_id_hex(id, hex);
    ASSERT(strlen(hex) == 32);
    ASSERT(fl_kq_delivery_id_parse(hex, back) == 0 && memcmp(id, back, sizeof id) == 0);
    ASSERT(fl_kq_delivery_id_parse("zz", back) != 0);
    hex[3] = 'g';
    ASSERT(fl_kq_delivery_id_parse(hex, back) != 0);
    return 0;
}

static int test_store_round_trip_and_leak(void) {
    fl_kq_store_t *s = NULL;
    fl_kq_delivery_t d, got, pending[4];
    uint8_t id[FL_KQ_DELIVERY_ID_LEN], id2[FL_KQ_DELIVERY_ID_LEN];
    uint8_t sig[64];
    const uint8_t letter[] = "sealed bytes the host cannot open";
    uint8_t *held = NULL;
    size_t held_len = 0;

    ASSERT(fl_kq_store_open(s_dir, &s) == FL_RESULT_OK);
    make_id(id, 10);
    make_id(id2, 20);
    memset(sig, 0x11, sizeof sig);
    fl_kq_delivery_init(&d, id, "Root.Flinstone.Mya", "Root.Flinstone.Steve", "plan.txt", NULL,
                        2000, 100);
    ASSERT(fl_kq_store_put(s, &d, letter, sizeof letter) == FL_RESULT_OK);
    ASSERT(fl_kq_store_put(s, &d, letter, sizeof letter) == FL_RESULT_BUSY);
    ASSERT(fl_kq_store_get(s, id, &got) == FL_RESULT_OK);
    ASSERT(strcmp(got.recipient, "Root.Flinstone.Steve") == 0 && got.potential == 1u);
    ASSERT(fl_kq_store_letter(s, id, &held, &held_len) == FL_RESULT_OK);
    ASSERT(held_len == sizeof letter && memcmp(held, letter, held_len) == 0);
    free(held);

    ASSERT(fl_kq_store_pending_for(s, "Root.Flinstone.Steve", 2001, pending, 4) == 1);
    ASSERT(fl_kq_store_pending_for(s, "Root.Flinstone.Eve", 2001, pending, 4) == 0);

    /* Fire and persist; the spike log grows. */
    ASSERT(fl_kq_delivery_integrate(&got, FL_KQ_SPIKE_COSIGN, sig, 2010) == FL_KQ_NEURON_FIRE);
    ASSERT(fl_kq_store_update(s, &got, FL_KQ_SPIKE_COSIGN, "Root.Flinstone.Steve", 2010) ==
           FL_RESULT_OK);
    ASSERT(fl_kq_store_get(s, id, &got) == FL_RESULT_OK && got.fired && got.has_cosig);
    ASSERT(fl_kq_store_spike_count(s, id) == 2);

    /* A second delivery that nobody co-signs leaks at expiry with its letter. */
    fl_kq_delivery_init(&d, id2, "Root.Flinstone.Mya", "Root.Flinstone.Steve", "old.txt", NULL,
                        2000, 10);
    ASSERT(fl_kq_store_put(s, &d, letter, sizeof letter) == FL_RESULT_OK);
    ASSERT(fl_kq_store_leak(s, 2005) == 0);
    ASSERT(fl_kq_store_leak(s, 2010) == 1);
    ASSERT(fl_kq_store_get(s, id2, &got) == FL_RESULT_NOENT);
    ASSERT(fl_kq_store_letter(s, id2, &held, &held_len) == FL_RESULT_NOENT);
    /* A fired delivery never leaks. */
    ASSERT(fl_kq_store_leak(s, 999999) == 0);
    ASSERT(fl_kq_store_get(s, id, &got) == FL_RESULT_OK);

    /* Acknowledged: off the pending list, letter deleted, log kept. */
    got.acknowledged = 1;
    ASSERT(fl_kq_store_update(s, &got, FL_KQ_SPIKE_ACK, "Root.Flinstone.Steve", 2020) == FL_RESULT_OK);
    ASSERT(fl_kq_store_forget_letter(s, id) == FL_RESULT_OK);
    ASSERT(fl_kq_store_letter(s, id, &held, &held_len) == FL_RESULT_NOENT);
    ASSERT(fl_kq_store_pending_for(s, "Root.Flinstone.Steve", 2021, pending, 4) == 0);
    ASSERT(fl_kq_store_spike_count(s, id) == 3);
    fl_kq_store_close(s);

    /* It reopens at the same schema version. */
    ASSERT(fl_kq_store_open(s_dir, &s) == FL_RESULT_OK);
    ASSERT(fl_kq_store_get(s, id, &got) == FL_RESULT_OK && got.acknowledged);
    fl_kq_store_close(s);
    return 0;
}

int main(void) {
    char tmpl[] = "/tmp/fl_kq_deliveryXXXXXX";
    int rc = 0;

    if (!mkdtemp(tmpl))
        return 1;
    snprintf(s_dir, sizeof s_dir, "%s", tmpl);
#define RUN(t) do { \
        if (t() != 0) { rc = 1; fprintf(stderr, "%s failed\n", #t); } \
        else printf("%s... OK\n", #t); \
    } while (0)
    RUN(test_neuron_fires_once_at_threshold);
    RUN(test_neuron_leaks_after_expiry);
    RUN(test_ids_round_trip);
    RUN(test_store_round_trip_and_leak);
    rmrf(s_dir);
    if (rc == 0)
        printf("All KeyQuorum delivery store tests passed.\n");
    return rc;
}
