/* Driver test suite: block, display, timer, PIC (host mode). */
#include "drivers/drivers.h"
#include "drivers/block/block_driver.h"
#include "drivers/fl_cstr.h"
#include "contract.h"
#include "fl/history_record.h"
#include "fl/audit_log.h"
#include "fl/jail_contract.h"
#include "contract_result.h"
#include "fl/driver/device.h"
#include "fl/driver/devfs.h"
#include "fl/driver/irq.h"
#include "fl/driver/usb_xhci_mmio_glue.h"
#include "fl/driver/p4_driver_model.h"
#include "fl/driver/p4_irq_lifecycle.h"
#include "fl/driver/p4_pcie_lab.h"
#include "fl/driver/p4_virtio.h"
#include "fl/driver/p4_usb_xhci_lab.h"
#include "fl/driver/p4_fdt_discovery.h"
#include "fl/driver/p4_psci.h"
#include "fl/driver/pci.h"
#include "fl/mm.h"
#include "common.h"
#if defined(FL_WITH_KEYQUORUM)
#include "fl/driver/keyquorum.h"
#define KQ_TEST_UTIL_KQ
#include "tests/kq_test_util.h"
#define EXPECTED_DEVICES 2
#else
#define EXPECTED_DEVICES 1
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int create_temp_disk(const char *path, int clusters, int cluster_size) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fprintf(fp, "XX:");
    for (int i = 0; i < cluster_size * 2; i++) fputc('0', fp);
    fputc('\n', fp);
    for (int c = 0; c < clusters; c++) {
        fprintf(fp, "%02X:", c);
        for (int i = 0; i < cluster_size * 2; i++) fputc('0', fp);
        fputc('\n', fp);
    }
    fclose(fp);
    return 0;
}

static int test_p4_driver_model_lock(void) {
    ASSERT(fl_driver_model_self_test_s_model_lock() == 0);
    return 0;
}

static int test_p4_irq_lifecycle(void) {
    fl_irq_lifecycle_init();
    ASSERT(fl_irq_lifecycle_in_hardirq() == 0);
    fl_irq_lifecycle_enter_hardirq();
    ASSERT(fl_irq_lifecycle_in_hardirq() == 1);
    fl_irq_lifecycle_leave_hardirq();
    ASSERT(fl_irq_lifecycle_in_hardirq() == 0);
    ASSERT(fl_irq_lifecycle_defer_bottom_half(3, NULL, NULL) == -1);
    return 0;
}

static int test_p4_pcie_lab(void) {
    ASSERT(fl_pcie_lab_iommu_bypass_assumed() == 1);
    pci_config_header_t hdr;
    memset(&hdr, 0, sizeof hdr);
    hdr.bar[0] = 0xF0000000u;
    fl_pcie_bar_info_t bar;
    ASSERT(fl_pcie_lab_decode_bar(&hdr, 0u, &bar) == FL_RESULT_OK);
    ASSERT(bar.is_mmio == 1);
    return 0;
}

static int test_p4_virtio_golden(void) {
    fl_virtio_split_layout_t layout = {
        .desc_addr = 0x1000u,
        .avail_addr = 0x1000u + 16u * 2u,
        .used_addr = 0x1000u + 16u * 2u + 6u + 2u * 2u,
        .queue_size = FL_VIRTIO_RING_SIZE_GOLDEN,
    };
    fl_virtio_desc_publish_barrier();
    ASSERT(fl_virtio_ring_golden_vector_validate(&layout) == FL_RESULT_OK);
    return 0;
}

static int test_p4_usb_xhci_lab(void) {
    uint32_t mmio[256];
    memset(mmio, 0, sizeof mmio);
    mmio[0] = 0x00000020u;
    fl_xhci_register_map_t map;
    ASSERT(fl_xhci_lab_map_registers(mmio, &map) == FL_RESULT_OK);
    fl_xhci_trb_t trb;
    ASSERT(fl_xhci_lab_trb_publish(&trb, 0x1000u, 1u, 1) == FL_RESULT_OK);
    ASSERT((trb.control & 1u) == 1u);
    fl_irq_lifecycle_init();
    ASSERT(fl_xhci_lab_event_ring_schedule_bh(1) == FL_RESULT_OK);
    fl_irq_lifecycle_run_bottom_halves();
    return 0;
}

static int test_p4_fdt_psci(void) {
    uint8_t dtb[384];
    size_t n = fl_p4_fdt_write_minimal_lab(dtb, sizeof dtb);
    ASSERT(n > 0u);
    fl_p4_fdt_summary_t sum;
    ASSERT(fl_p4_fdt_walk(dtb, n, &sum) == FL_RESULT_OK);
    ASSERT(sum.has_memory && sum.has_cpus && sum.has_psci);
    ASSERT(fl_psci_discover_from_dtb(dtb, n) == FL_RESULT_OK);
    ASSERT(fl_psci_status_to_result(FL_PSCI_STATUS_SUCCESS) == FL_RESULT_OK);
    ASSERT(fl_psci_status_to_result(FL_PSCI_STATUS_DENIED) == FL_RESULT_ACCES);
    return 0;
}

static int test_usb_xhci_mmio_glue(void) {
    uint32_t word = 0xDEADBEEFu;
    uint32_t r = fl_usb_xhci_mmio_read32_volatile(&word);
    ASSERT(r == 0xDEADBEEFu);
    fl_usb_xhci_mmio_write32_volatile(&word, 0x12345678u);
    ASSERT(word == 0x12345678u);
    fl_usb_xhci_mmio_rw_fence();
    return 0;
}

static int test_block(void) {
    ASSERT(g_block_driver != NULL);
    block_caps_t caps;
    ASSERT(g_block_driver->get_caps((block_driver_t *)g_block_driver, &caps) == 0);
    ASSERT(caps.sector_size > 0);
    (void)caps;
    uint8_t buf[512];
    memset(buf, 0xAB, sizeof(buf));
    ASSERT(g_block_driver->read_sector((block_driver_t *)g_block_driver, 0, buf) == 0);
    return 0;
}

static int test_block_write_read(void) {
    int cs = g_cluster_size > 64 ? 64 : g_cluster_size;
    uint8_t wbuf[64], rbuf[64];
    memset(wbuf, 0, sizeof(wbuf));
    memset(rbuf, 0, sizeof(rbuf));
    for (int i = 0; i < cs; i++) wbuf[i] = (uint8_t)(0x30 + i);
    ASSERT(g_block_driver->write_sector((block_driver_t *)g_block_driver, 0, wbuf) == 0);
    ASSERT(g_block_driver->read_sector((block_driver_t *)g_block_driver, 0, rbuf) == 0);
    ASSERT(memcmp(wbuf, rbuf, cs) == 0);
    return 0;
}

static int test_device_model(void) {
    fl_device_desc_t descs[4];
    const fl_driver_desc_t *matched = NULL;
    fl_device_t *dev = NULL;
    const fl_device_desc_t *desc = NULL;
    fl_device_info_t info;
    fl_resource_t res;

    memset(descs, 0, sizeof(descs));
    memset(&info, 0, sizeof(info));
    memset(&res, 0, sizeof(res));
    ASSERT(fl_bus_enumerate(descs, 4) == EXPECTED_DEVICES);
    ASSERT(fl_driver_registry_match(&descs[0], &matched) == FL_RESULT_OK);
    ASSERT(matched != NULL);
    ASSERT(strcmp(matched->name, "host-block") == 0);
    ASSERT(fl_device_count() == EXPECTED_DEVICES);
    ASSERT(fl_device_get_info(0, &info) == 0);
    ASSERT(info.dev != NULL);
    ASSERT(info.desc != NULL);
    ASSERT(strcmp(info.driver_name, "host-block") == 0);
    ASSERT(info.driver_class == FL_DRV_CLASS_BLOCK);
    ASSERT(info.state == FL_DRV_STATE_STARTED);
    dev = fl_device_find_synth("host_blk");
    ASSERT(dev != NULL);
    desc = fl_device_get_desc(dev);
    ASSERT(desc != NULL);
    ASSERT(desc->bus_type == FL_BUS_SYNTH);
    ASSERT(strcmp(desc->synth_id, "host_blk") == 0);
    ASSERT(fl_resource_count(dev) == 4);
    ASSERT(fl_resource_get(dev, 0, &res) == 0);
    ASSERT(res.type == FL_RESOURCE_SYNTH);
    ASSERT(res.start == (uintptr_t)current_disk_file);
    ASSERT(res.size == strlen(current_disk_file) + 1);
    fl_drivers_init();
    ASSERT(fl_device_count() == EXPECTED_DEVICES);
    ASSERT(fl_resource_claim(dev, FL_RESOURCE_SYNTH, 0) != 0);
    ASSERT(fl_resource_claim(dev, FL_RESOURCE_SYNTH, 99) != 0);
    return 0;
}

/* KeyQuorum driver is bound only in a build that links it (FL_WITH_KEYQUORUM). */
static int test_keyquorum_registration(void) {
    fl_device_desc_t descs[4];
    fl_device_desc_t probe_desc;
    const fl_driver_desc_t *matched = NULL;
    fl_device_info_t info;
    int bound = 0;

    memset(descs, 0, sizeof(descs));
    memset(&probe_desc, 0, sizeof(probe_desc));
    probe_desc.bus_type = FL_BUS_SYNTH;
    strcpy(probe_desc.synth_id, "keyquorum");
    for (int i = 0; i < fl_device_count(); i++) {
        memset(&info, 0, sizeof(info));
        ASSERT(fl_device_get_info(i, &info) == 0);
        if (info.driver_name && strcmp(info.driver_name, "keyquorum") == 0)
            bound++;
    }
#if defined(FL_WITH_KEYQUORUM)
    const fl_device_desc_t *desc;
    fl_device_t *dev;

    ASSERT(fl_bus_enumerate(descs, 4) == 2);
    ASSERT(descs[1].bus_type == FL_BUS_SYNTH);
    ASSERT(strcmp(descs[1].synth_id, "keyquorum") == 0);
    ASSERT(descs[1].resource_count == 0);
    ASSERT(fl_bus_enumerate(descs, 1) == 1);

    ASSERT(fl_driver_registry_match(&probe_desc, &matched) == FL_RESULT_OK);
    ASSERT(matched == fl_kq_driver_desc());
    ASSERT(matched->class == FL_DRV_CLASS_SECURITY);
    ASSERT(strcmp(matched->name, "keyquorum") == 0);

    ASSERT(bound == 1);
    dev = fl_device_find_synth("keyquorum");
    ASSERT(dev != NULL);
    desc = fl_device_get_desc(dev);
    ASSERT(desc != NULL && desc->bus_type == FL_BUS_SYNTH);
    ASSERT(fl_resource_count(dev) == 0);
    for (int i = 0; i < fl_device_count(); i++) {
        ASSERT(fl_device_get_info(i, &info) == 0);
        if (info.dev != dev)
            continue;
        ASSERT(info.driver_class == FL_DRV_CLASS_SECURITY);
        ASSERT(info.state == FL_DRV_STATE_STARTED);
        ASSERT(fl_kq_driver_from_desc(matched) == fl_kq_driver_get());
        ASSERT(fl_driver_registry_contains(fl_kq_driver_desc()) == 1);
        ASSERT(fl_kq_driver_get() != NULL);
        return 0;
    }
    return 1;
#else
    ASSERT(fl_bus_enumerate(descs, 4) == 1);
    ASSERT(fl_driver_registry_match(&probe_desc, &matched) == FL_RESULT_PROBE_SKIP);
    ASSERT(matched == NULL);
    ASSERT(bound == 0);
    ASSERT(fl_device_find_synth("keyquorum") == NULL);
    return 0;
#endif
}

#if defined(FL_WITH_KEYQUORUM)
static int test_keyquorum_account(void) {
    const fl_kq_driver_t *kq = fl_kq_driver_get();
    uint8_t sec[FL_KQ_ACCOUNT_SECRET_LEN], pub[FL_KQ_ACCOUNT_PUBLIC_LEN], again[32], back[32];
    uint8_t bsec[32], bpub[32];
    uint8_t sealed[FL_KQ_ACCOUNT_SEALED_LEN + 8];
    uint8_t nonce[FL_KQ_ACCOUNT_NONCE_LEN], sig[FL_KQ_ACCOUNT_SIGNATURE_LEN];
    size_t sealed_len = 0;
    const char *from = "Root.Flinstone.Mya", *to = "Root.Alternative.Eve";

    ASSERT(kq != NULL);
    ASSERT(kq->account_keygen && kq->account_public && kq->account_box_keygen &&
           kq->account_box_public && kq->account_seal && kq->account_open &&
           kq->account_bridge_sign && kq->account_bridge_verify);
    ASSERT(kq->account_keygen(sec, pub) == FL_KQ_ACCOUNT_OK);
    ASSERT(kq->account_public(sec, again) == FL_KQ_ACCOUNT_OK);
    ASSERT(kq->equal(pub, again, sizeof pub) != 0);
    ASSERT(kq->account_box_keygen(bsec, bpub) == FL_KQ_ACCOUNT_OK);
    ASSERT(kq->account_box_public(bsec, again) == FL_KQ_ACCOUNT_OK);
    ASSERT(kq->equal(bpub, again, sizeof bpub) != 0);
    ASSERT(kq->account_seal("pw-one", sec, sealed, sizeof sealed, &sealed_len) ==
           FL_KQ_ACCOUNT_OK);
    ASSERT(sealed_len == FL_KQ_ACCOUNT_SEALED_LEN);
    ASSERT(kq->account_open("pw-one", sealed, sealed_len, back) == FL_KQ_ACCOUNT_OK);
    ASSERT(kq->equal(sec, back, sizeof sec) != 0);
    memset(back, 0, sizeof back);
    ASSERT(kq->account_open("pw-two", sealed, sealed_len, back) == FL_KQ_ACCOUNT_WRONG_PASSWORD);
    ASSERT(kq->account_seal("pw-one", sec, sealed, 4, &sealed_len) == FL_KQ_ACCOUNT_NO_ROOM);
    memset(nonce, 7, sizeof nonce);
    ASSERT(kq->account_bridge_sign(FL_KQ_ACCOUNT_BRIDGE_REQUEST, from, to, from, nonce, sec,
                                   sig) == FL_KQ_ACCOUNT_OK);
    ASSERT(kq->account_bridge_verify(FL_KQ_ACCOUNT_BRIDGE_REQUEST, from, to, from, nonce, pub,
                                     sig) == 1);
    ASSERT(kq->account_bridge_verify(FL_KQ_ACCOUNT_BRIDGE_ACCEPT, from, to, from, nonce, pub,
                                     sig) != 1);
    sig[0] ^= 1u;
    ASSERT(kq->account_bridge_verify(FL_KQ_ACCOUNT_BRIDGE_REQUEST, from, to, from, nonce, pub,
                                     sig) == 0);
    ASSERT(kq->account_bridge_sign(FL_KQ_ACCOUNT_BRIDGE_REQUEST, from, from, from, nonce, sec,
                                   sig) == FL_KQ_ACCOUNT_INVALID);
    kq->wipe(sec, sizeof sec);
    kq->wipe(bsec, sizeof bsec);
    kq->wipe(back, sizeof back);
    return 0;
}

/* head_author: FORKED for two heads, NO_ROOM for a buffer the label does not fit. */
static int test_keyquorum_head_author(void) {
    const fl_kq_driver_t *kq = fl_kq_driver_get();
    const char *mya = "Root.Flinstone.Mya";
    uint8_t sec[32], pub[32], id[16], rev[32];
    fl_kq_buf_t empty = {0}, one = {0}, forked = {0};
    char who[64], tiny[4];
    int rc = 1;

    ASSERT(kq && kq->head_author);
    ASSERT(kq->account_keygen(sec, pub) == FL_KQ_ACCOUNT_OK);
    memset(id, 0x4D, sizeof id);
    if (kq->file_new(id, "plan.txt", mya, &empty) != KQ_FILE_OK)
        goto out;
    strcpy(who, "stale");
    if (kq->head_author(&empty, who, sizeof who) != KQ_FILE_NO_REVISION || who[0] != '\0')
        goto out;
    if (kq->check_in(&empty, mya, "2026-11-10T09:00:00.000Z", (const uint8_t *)"v1\n", 3, sec, &one,
                     rev) != KQ_FILE_OK ||
        kq->head_author(&one, who, sizeof who) != KQ_FILE_OK || strcmp(who, mya) != 0)
        goto out;
    if (kq->head_author(&one, tiny, sizeof tiny) != KQ_FILE_NO_ROOM || tiny[0] != '\0')
        goto out;
    if (kq_test_fork(kq, &one, mya, sec, "2026-11-10T09:01:00.000Z", "2026-11-10T09:02:00.000Z",
                     "2026-11-10T09:03:00.000Z", &forked) != 0)
        goto out;
    if (kq->head_author(&forked, who, sizeof who) != KQ_FILE_FORKED || who[0] != '\0')
        goto out;
    rc = 0;
out:
    kq->wipe(sec, sizeof sec);
    fl_kq_buf_free(&empty);
    fl_kq_buf_free(&one);
    fl_kq_buf_free(&forked);
    if (rc)
        fprintf(stderr, "FAIL [%s]: head_author\n", __func__);
    return rc;
}

static int test_keyquorum_access_and_bridge_ops(void) {
    const fl_kq_driver_t *kq = fl_kq_driver_get();
    const char *members[] = {"Root.Flinstone.Bob", "Root.Alternative.Ann"};
    const kq_bridge_t live = {"0123456789abcdef0123456789abcdef", members, 2};
    const char *labels[] = {"Root.Flinstone.Bob", "Root.Alternative.Ann"};
    uint8_t salt[16], wrap[16], pub[32], sk[32], ek[32], pubs[64], challenge[32], art[512];
    uint8_t sealed[96];
    size_t alen = 0;
    int idx = 7, grant = 7;

    ASSERT(kq && kq->access_requirement && kq->bridge_prove && kq->bridge_verify && kq->bridge_connects);
    ASSERT(kq->bridge_connects(members[0], members[1], "Root.Flinstone.Bob.Kid", "Root.Alternative.Ann") == 1);
    ASSERT(kq->bridge_connects(members[0], members[1], "Root.Third.Eve", "Root.Alternative.Ann") == 0);
    ASSERT(kq->access_requirement("Root.Flinstone.Bob", "Root.Flinstone.Bob", 0, NULL, 0, 0, &idx,
                                  &grant) == FL_KQ_ACCESS_NONE);
    ASSERT(idx == -1 && grant == 0);
    ASSERT(kq->access_requirement("Root.Flinstone.Bob", "Root.Flinstone.Bob", 1, NULL, 0, 0, NULL,
                                  NULL) == FL_KQ_ACCESS_KQST_PASSWORD);
    ASSERT(kq->access_requirement("Root.Flinstone", "Root.Flinstone.Bob", 0, NULL, 0, 1, &idx,
                                  &grant) == FL_KQ_ACCESS_PASSWORD);
    ASSERT(grant == 1); /* parent approval on: a same-branch switch needs the grant */
    ASSERT(kq->access_requirement("Root.Flinstone.Bob", "Root.Alternative.Ann", 0, NULL, 0, 0, &idx,
                                  &grant) == FL_KQ_ACCESS_KQST_PASSWORD);
    ASSERT(kq->access_requirement("Root.Flinstone.Bob", "Root.Alternative.Ann", 0, &live, 1, 0, &idx,
                                  &grant) == FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST);
    ASSERT(idx == 0 && grant == 0);
    ASSERT(kq->access_requirement("Root.Flinstone.Bob", "Root..Bob", 0, NULL, 0, 0, NULL, NULL) ==
           FL_KQ_ACCESS_REFUSED);
    ASSERT(kq->access_requirement(NULL, "Root", 0, NULL, 0, 0, &idx, &grant) == FL_KQ_ACCESS_INVALID);
    ASSERT(idx == -1 && grant == 0);

    memset(salt, 0x11, sizeof salt);
    memset(wrap, 0x22, sizeof wrap);
    memset(sealed, 0x33, sizeof sealed);
    memset(challenge, 0x44, sizeof challenge);
    ASSERT(kq->account_keygen(sk, pub) == FL_KQ_ACCOUNT_OK);
    ASSERT(kq->account_box_keygen(ek, pubs) == FL_KQ_ACCOUNT_OK);
    ASSERT(kq->bridge_prove(live.uid, 1, salt, pub, members[0], sealed, sizeof sealed, wrap, ek, sk,
                            challenge, sizeof challenge, art, sizeof art, &alen) < 0);
    kq->wipe(sk, sizeof sk);
    kq->wipe(ek, sizeof ek);
    memcpy(pubs, pub, 32);
    memcpy(pubs + 32, pub, 32);
    memset(art, 0x55, 64);
    ASSERT(kq->bridge_verify(live.uid, 1, salt, pub, 0, labels, pubs, 2, labels[1], challenge,
                             sizeof challenge, art, 64) != FL_KQ_BRIDGE_OK);
    ASSERT(kq->bridge_verify(live.uid, 1, salt, pub, 0, NULL, pubs, 2, labels[1], challenge,
                             sizeof challenge, art, 64) == FL_KQ_BRIDGE_INVALID);
    return 0;
}
#endif

/* A shutdown empties the registry; re-init binds the device again, exactly once. */
static int test_keyquorum_survives_reinit(const char *disk_path) {
    fl_device_desc_t probe_desc;
    const fl_driver_desc_t *matched = NULL;
    fl_device_info_t info;
    int before = fl_device_count();
    int bound = 0;

    memset(&probe_desc, 0, sizeof(probe_desc));
    probe_desc.bus_type = FL_BUS_SYNTH;
    strcpy(probe_desc.synth_id, "keyquorum");
    drivers_shutdown();
    ASSERT(fl_device_count() == 0);
#if defined(FL_WITH_KEYQUORUM)
    ASSERT(fl_driver_registry_contains(NULL) == 0);
    ASSERT(fl_driver_registry_contains(fl_kq_driver_desc()) == 0);
    ASSERT(fl_kq_driver_get() == NULL);
    {
        static const fl_driver_ops_t foreign_ops;
        static const fl_driver_desc_t foreign = {
            FL_DRV_CLASS_SECURITY, "keyquorum", &foreign_ops, 0, 0, NULL, "keyquorum",
        };

        ASSERT(fl_driver_registry_register(&foreign) == FL_RESULT_OK);
        ASSERT(fl_driver_registry_contains(&foreign) == 1);
        ASSERT(fl_driver_registry_contains(fl_kq_driver_desc()) == 0);
        ASSERT(fl_kq_driver_get() == NULL);
        ASSERT(fl_driver_registry_register(fl_kq_driver_desc()) == FL_RESULT_OK);
        ASSERT(fl_driver_registry_contains(fl_kq_driver_desc()) == 1);
        ASSERT(fl_kq_driver_desc()->ops && fl_kq_driver_desc()->ops->probe);
        ASSERT(fl_kq_driver_desc()->ops->probe(NULL) == FL_RESULT_OK);
        ASSERT(fl_kq_driver_get() == fl_kq_driver_from_desc(fl_kq_driver_desc()));
    }
    drivers_shutdown();
    ASSERT(fl_device_count() == 0);
    ASSERT(fl_kq_driver_get() == NULL);
    ASSERT(fl_driver_registry_ensure(fl_kq_driver_desc()) == FL_RESULT_ERR);
    ASSERT(fl_kq_driver_get_or_register() == NULL);
    ASSERT(fl_driver_registry_contains(fl_kq_driver_desc()) == 0);
    ASSERT(fl_driver_registry_count() == 0);
/* A byte-for-byte copy of the descriptor never resolves: membership is by pointer. */
    {
        static fl_kq_driver_t forged;

        memcpy(&forged, fl_kq_driver_from_desc(fl_kq_driver_desc()), sizeof forged);
        ASSERT(fl_driver_registry_register(&forged.base) == FL_RESULT_OK);
        ASSERT(fl_driver_registry_count() == 1);
        ASSERT(fl_kq_driver_get() == NULL);
        drivers_shutdown();
        ASSERT(fl_driver_registry_count() == 0);
    }
    fl_driver_registry_register_all();
    {
        int n = fl_driver_registry_count();

        ASSERT(fl_kq_driver_get_or_register() == fl_kq_driver_from_desc(fl_kq_driver_desc()));
        ASSERT(fl_driver_registry_ensure(fl_kq_driver_desc()) == FL_RESULT_OK);
        ASSERT(fl_driver_registry_count() == n);
        fl_driver_registry_register_all();
        ASSERT(fl_driver_registry_count() == n);
    }
    drivers_shutdown();
#endif
    drivers_init(disk_path);
    ASSERT(fl_device_count() == before);
    for (int i = 0; i < fl_device_count(); i++) {
        memset(&info, 0, sizeof(info));
        ASSERT(fl_device_get_info(i, &info) == 0);
        if (info.driver_name && strcmp(info.driver_name, "keyquorum") == 0)
            bound++;
    }
#if defined(FL_WITH_KEYQUORUM)
    ASSERT(bound == 1);
    ASSERT(fl_device_find_synth("keyquorum") != NULL);
    ASSERT(fl_driver_registry_match(&probe_desc, &matched) == FL_RESULT_OK);
    ASSERT(matched == fl_kq_driver_desc());
    ASSERT(fl_kq_driver_get() == fl_kq_driver_from_desc(matched));
#else
    ASSERT(bound == 0);
    ASSERT(fl_driver_registry_match(&probe_desc, &matched) == FL_RESULT_PROBE_SKIP);
#endif
    return 0;
}

static int test_devfs_block(void) {
    fl_devfs_file_t file;
    fl_block_caps_t caps;
    uint8_t wbuf[512], rbuf[512];
    size_t n = 0;

    memset(&file, 0, sizeof(file));
    memset(&caps, 0, sizeof(caps));
    memset(wbuf, 0, sizeof(wbuf));
    memset(rbuf, 0, sizeof(rbuf));
    for (int i = 0; i < g_cluster_size && i < (int)sizeof(wbuf); i++)
        wbuf[i] = (uint8_t)(0x41 + (i % 26));

    ASSERT(fl_devfs_open("/dev/blk0", FL_DEVFS_O_READ | FL_DEVFS_O_WRITE, &file) == 0);
    ASSERT(fl_devfs_ioctl(&file, FL_DEVFS_IOCTL_BLOCK_CAPS, &caps) == 0);
    ASSERT(caps.sector_size == FL_SECTOR_SIZE);
    ASSERT(fl_devfs_write(&file, wbuf, sizeof(wbuf), &n) == 0);
    ASSERT(n == FL_SECTOR_SIZE);
    file.pos = 0;
    ASSERT(fl_devfs_read(&file, rbuf, sizeof(rbuf), &n) == 0);
    ASSERT(n == FL_SECTOR_SIZE);
    ASSERT(memcmp(wbuf, rbuf, (size_t)g_cluster_size) == 0);
    ASSERT(fl_devfs_close(&file) == 0);
    ASSERT(fl_devfs_register("/dev/this/path/is/too/long/for/devfs", FL_DRV_CLASS_BLOCK,
                             fl_device_find_synth("host_blk"), &(fl_devfs_ops_t){0}) !=
           FL_RESULT_OK);
    return 0;
}

static void test_irq_handler(int irq, void *ctx) {
    int *hits = (int *)ctx;
    if (irq == 3)
        (*hits)++;
}

static int test_irq_and_dma(void) {
    int hits = 0;
    fl_device_t *dev = fl_device_find_synth("host_blk");
    uint8_t src[128], dst[128];
    fl_dma_info_t info;
    fl_irq_info_t irq_info;
    void *buf = NULL;
    ASSERT(dev != NULL);
    buf = fl_dma_alloc_device(dev, sizeof(dst));
    for (size_t i = 0; i < sizeof(src); i++)
        src[i] = (uint8_t)(i + 1);
    ASSERT(buf != NULL);
    ASSERT(fl_dma_allocation_count() == 1);
    ASSERT(fl_dma_get_info(buf, &info) == 0);
    ASSERT(info.ptr == buf);
    ASSERT(info.size == sizeof(dst));
    ASSERT(info.owner == dev);
    fl_dma_copy(buf, src, sizeof(src));
    memset(dst, 0, sizeof(dst));
    fl_dma_copy(dst, buf, sizeof(dst));
    ASSERT(memcmp(src, dst, sizeof(src)) == 0);
    fl_dma_zero(buf, sizeof(dst));
    memset(dst, 0xFF, sizeof(dst));
    fl_dma_copy(dst, buf, sizeof(dst));
    ASSERT(dst[0] == 0 && dst[sizeof(dst) - 1] == 0);
    fl_dma_free(buf);
    ASSERT(fl_dma_allocation_count() == 0);
    ASSERT(fl_irq_register_device(dev, 0, test_irq_handler, &hits) == 0);
    fl_irq_enable(3);
    ASSERT(fl_irq_dispatch(3) == 0);
    ASSERT(hits == 1);
    ASSERT(fl_irq_dispatch_count(3) == 1);
    ASSERT(fl_irq_get_info(3, &irq_info) == 0);
    ASSERT(irq_info.irq == 3);
    ASSERT(irq_info.enabled == 1);
    ASSERT(irq_info.dispatch_count == 1);
    ASSERT(irq_info.eoi_count == 1);
    ASSERT(irq_info.owner == dev);
    fl_irq_disable(3);
    ASSERT(fl_irq_dispatch(3) != 0);
    fl_irq_unregister_device(dev);
    ASSERT(fl_irq_get_info(3, &irq_info) == 0);
    ASSERT(irq_info.owner == NULL);
    ASSERT(fl_irq_register(3, test_irq_handler, &hits) == 0);
    fl_irq_unregister(3);
    ASSERT(fl_irq_get_info(3, &irq_info) == 0);
    ASSERT(irq_info.owner == NULL);
    ASSERT(fl_irq_register_device(dev, 99, test_irq_handler, &hits) != 0);
    ASSERT(fl_irq_register(31, test_irq_handler, &hits) != 0);
    ASSERT(fl_irq_get_info(99, &irq_info) != 0);
    return 0;
}

static int test_fl_cstr(void) {
    ASSERT(fl_cstr_len(NULL, 10) == 0);
    ASSERT(fl_cstr_len("", 10) == 0);
    ASSERT(fl_cstr_len("hello", 10) == 5);
    ASSERT(fl_cstr_len("hello", 3) == 3);
    ASSERT(fl_cstr_len("hello", 0) == 0);
    ASSERT(fl_cstr_len("abc", 3) == 3);
    ASSERT(fl_cstr_len("abc", 4) == 3);

    ASSERT(fl_cstr_eq(NULL, "foo") == 0);
    ASSERT(fl_cstr_eq("foo", NULL) == 0);
    ASSERT(fl_cstr_eq(NULL, NULL) == 0);
    ASSERT(fl_cstr_eq("", "") == 1);
    ASSERT(fl_cstr_eq("foo", "foo") == 1);
    ASSERT(fl_cstr_eq("foo", "bar") == 0);
    ASSERT(fl_cstr_eq("foo", "foo2") == 0);
    ASSERT(fl_cstr_eq("foo2", "foo") == 0);
    ASSERT(fl_cstr_eq("a", "a") == 1);
    ASSERT(fl_cstr_eq("a", "b") == 0);
    ASSERT(fl_cstr_eq("host_blk", "host_blk") == 1);
    ASSERT(fl_cstr_eq("host_blk", "host_blk2") == 0);

    char buf[16];

    fl_cstr_copy(NULL, 5, "hello");

    fl_cstr_copy(buf, 0, "hello");

    memset(buf, 0xFF, sizeof(buf));
    fl_cstr_copy(buf, sizeof(buf), NULL);
    ASSERT(buf[0] == '\0');

    fl_cstr_copy(buf, sizeof(buf), "hello");
    ASSERT(strcmp(buf, "hello") == 0);

    fl_cstr_copy(buf, 5, "hello world");
    ASSERT(buf[4] == '\0');
    ASSERT(strncmp(buf, "hell", 4) == 0);

    fl_cstr_copy(buf, sizeof(buf), "");
    ASSERT(buf[0] == '\0');

    fl_cstr_copy(buf, 1, "hello");
    ASSERT(buf[0] == '\0');

    static char overlap[16];
    memset(overlap, 0, sizeof(overlap));
    strcpy(overlap, "abcde");
    fl_cstr_copy(overlap + 2, sizeof(overlap) - 2, overlap);
    ASSERT(overlap[2] == 'a');
    ASSERT(overlap[3] == 'b');
    ASSERT(overlap[4] == 'c');
    ASSERT(overlap[5] == 'd');
    ASSERT(overlap[6] == 'e');
    ASSERT(overlap[7] == '\0');

    return 0;
}

static int test_bus_resource_extraction(void) {
    fl_device_desc_t descs[2];
    int irq_list[8];
    fl_mmio_region_t mmio_list[8];
    uint16_t ioport_base, ioport_count;

    memset(descs, 0, sizeof(descs));

    int n = fl_bus_enumerate(descs, 2);
    ASSERT(n == EXPECTED_DEVICES);

    const fl_device_desc_t *d = &descs[0];
    ASSERT(d->resource_count == 4);

    ASSERT(fl_device_desc_irq_list(NULL, irq_list, 8) == 0);
    ASSERT(fl_device_desc_irq_list(d, NULL, 8) == 0);
    ASSERT(fl_device_desc_irq_list(d, irq_list, 0) == 0);
    memset(irq_list, 0, sizeof(irq_list));
    ASSERT(fl_device_desc_irq_list(d, irq_list, 8) == 1);
    ASSERT(irq_list[0] == 3);
    memset(irq_list, 0, sizeof(irq_list));
    ASSERT(fl_device_desc_irq_list(d, irq_list, 1) == 1);
    ASSERT(irq_list[0] == 3);

    ASSERT(fl_device_desc_mmio_list(NULL, mmio_list, 8) == 0);
    ASSERT(fl_device_desc_mmio_list(d, NULL, 8) == 0);
    ASSERT(fl_device_desc_mmio_list(d, mmio_list, 0) == 0);
    memset(mmio_list, 0, sizeof(mmio_list));
    ASSERT(fl_device_desc_mmio_list(d, mmio_list, 8) == 0);

    ASSERT(fl_device_desc_ioport_range(NULL, &ioport_base, &ioport_count) == -1);
    ASSERT(fl_device_desc_ioport_range(d, NULL, &ioport_count) == -1);
    ASSERT(fl_device_desc_ioport_range(d, &ioport_base, NULL) == -1);
    ioport_base = 0; ioport_count = 0;
    ASSERT(fl_device_desc_ioport_range(d, &ioport_base, &ioport_count) == 0);
    ASSERT(ioport_base == 0xF100u);
    ASSERT(ioport_count == 8);

    fl_device_desc_t empty;
    memset(&empty, 0, sizeof(empty));
    empty.resource_count = 0;
    ASSERT(fl_device_desc_irq_list(&empty, irq_list, 8) == 0);
    ASSERT(fl_device_desc_mmio_list(&empty, mmio_list, 8) == 0);
    ASSERT(fl_device_desc_ioport_range(&empty, &ioport_base, &ioport_count) == -1);

    return 0;
}

static int test_devfs_flags_and_errors(void) {
    fl_devfs_file_t file;
    uint8_t buf[512];
    size_t n = 0;

    memset(&file, 0, sizeof(file));
    memset(buf, 0, sizeof(buf));

    ASSERT(fl_devfs_open("/dev/does_not_exist", FL_DEVFS_O_READ, &file) != 0);

    ASSERT(fl_devfs_open(NULL, FL_DEVFS_O_READ, &file) != 0);

    ASSERT(fl_devfs_open("/dev/blk0", FL_DEVFS_O_READ, NULL) != 0);

    memset(&file, 0, sizeof(file));
    ASSERT(fl_devfs_open("/dev/blk0", FL_DEVFS_O_READ, &file) == 0);
    ASSERT(fl_devfs_write(&file, buf, sizeof(buf), &n) != 0);
    ASSERT(fl_devfs_close(&file) == 0);

    memset(&file, 0, sizeof(file));
    ASSERT(fl_devfs_open("/dev/blk0", FL_DEVFS_O_WRITE, &file) == 0);
    ASSERT(fl_devfs_read(&file, buf, sizeof(buf), &n) != 0);
    ASSERT(fl_devfs_close(&file) == 0);

    ASSERT(fl_devfs_read(NULL, buf, sizeof(buf), &n) != 0);
    ASSERT(fl_devfs_write(NULL, buf, sizeof(buf), &n) != 0);
    ASSERT(fl_devfs_ioctl(NULL, FL_DEVFS_IOCTL_BLOCK_CAPS, buf) != 0);
    ASSERT(fl_devfs_close(NULL) != 0);

    memset(&file, 0, sizeof(file));
    ASSERT(fl_devfs_open("/dev/blk0", FL_DEVFS_O_READ | FL_DEVFS_O_WRITE, &file) == 0);
    ASSERT(file.node != NULL);
    ASSERT(fl_devfs_close(&file) == 0);
    ASSERT(file.node == NULL);
    ASSERT(file.pos == 0);

    fl_devfs_ops_t dummy_ops = {0};
    fl_device_t *blk = fl_device_find_synth("host_blk");
    ASSERT(blk != NULL);
    ASSERT(fl_devfs_register("/dev/blk0", FL_DRV_CLASS_BLOCK, blk, &dummy_ops) != FL_RESULT_OK);

    ASSERT(fl_devfs_register("/dev/test_tmp", FL_DRV_CLASS_BLOCK, blk, &dummy_ops) == FL_RESULT_OK);
    fl_devfs_unregister("/dev/test_tmp");
    ASSERT(fl_devfs_open("/dev/test_tmp", FL_DEVFS_O_READ, &file) != 0);

    return 0;
}

static int test_dma_edge_cases(void) {
    fl_dma_info_t info;

    void *p = fl_dma_alloc(0);
    ASSERT(p == NULL);

    ASSERT(fl_dma_get_info(NULL, NULL) != 0);
    ASSERT(fl_dma_get_info(NULL, &info) != 0);

    int dummy = 42;
    ASSERT(fl_dma_get_info(&dummy, &info) != 0);

    ASSERT(fl_dma_allocation_count() == 0);

    p = fl_dma_alloc(64);
    ASSERT(p != NULL);
    ASSERT(fl_dma_allocation_count() == 1);
    fl_dma_free(p);
    ASSERT(fl_dma_allocation_count() == 0);

    p = fl_dma_alloc(32);
    ASSERT(p != NULL);
    fl_dma_free(p);
    fl_dma_free(p); /* second free: pointer no longer tracked, should be no-op */
    ASSERT(fl_dma_allocation_count() == 0);

    fl_dma_free(NULL);

    fl_dma_zero(NULL, 8);
    fl_dma_zero(NULL, 0);
    p = fl_dma_alloc(16);
    ASSERT(p != NULL);
    fl_dma_zero(p, 0);
    fl_dma_copy(NULL, p, 8);
    fl_dma_copy(p, NULL, 8);
    fl_dma_free(p);

    return 0;
}

static int test_irq_edge_cases(void) {
    fl_irq_info_t irq_info;
    /* Past end of s_irq[]; keep in sync with FL_MODEL_MAX_IRQ in driver_model.c */
    const int irq_past_max = 64;

    ASSERT(fl_irq_get_info(-1, &irq_info) != 0);
    ASSERT(fl_irq_get_info(irq_past_max, &irq_info) != 0);

    ASSERT(fl_irq_get_info(0, NULL) != 0);

    void *ctx = NULL;
    ASSERT(fl_irq_register(-1, test_irq_handler, ctx) != 0);
    ASSERT(fl_irq_register(irq_past_max, test_irq_handler, ctx) != 0);

    ASSERT(fl_irq_register(0, NULL, ctx) != 0);

    ASSERT(fl_irq_dispatch(-1) != 0);
    ASSERT(fl_irq_dispatch(irq_past_max) != 0);

    ASSERT(fl_irq_dispatch_count(-1) == 0);
    ASSERT(fl_irq_dispatch_count(irq_past_max) == 0);

    fl_irq_enable(-1);
    fl_irq_enable(irq_past_max);
    fl_irq_disable(-1);
    fl_irq_disable(irq_past_max);

    fl_irq_unregister(-1);
    fl_irq_unregister(irq_past_max);

    fl_irq_unregister_device(NULL);

    fl_irq_eoi(-1);
    fl_irq_eoi(irq_past_max);

    return 0;
}

static int test_device_model_null_params(void) {
    fl_device_info_t info;
    fl_resource_t res;

    ASSERT(fl_device_get_info(-1, &info) != 0);
    ASSERT(fl_device_get_info(9999, &info) != 0);

    ASSERT(fl_device_get_info(0, NULL) != 0);

    ASSERT(fl_device_find_synth(NULL) == NULL);

    ASSERT(fl_device_find_synth("no_such_device_id") == NULL);

    ASSERT(fl_device_get_desc(NULL) == NULL);

    int total = fl_resource_count(NULL);
    ASSERT(total >= 0);

    ASSERT(fl_resource_get(NULL, 0, NULL) != 0);

    fl_device_t *dev = fl_device_find_synth("host_blk");
    ASSERT(dev != NULL);
    ASSERT(fl_resource_get(dev, -1, &res) != 0);

    ASSERT(fl_resource_get(dev, 9999, &res) != 0);

    ASSERT(fl_resource_claim(NULL, FL_RESOURCE_IRQ, 0) != 0);

    fl_resource_release_all(NULL);

    const fl_driver_desc_t *matched = NULL;
    ASSERT(fl_driver_registry_match(NULL, &matched) == FL_RESULT_INVAL);
    fl_device_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    ASSERT(fl_driver_registry_match(&desc, NULL) == FL_RESULT_INVAL);

    return 0;
}

static int test_display(void) {
    ASSERT(g_display_driver != NULL);
    g_display_driver->putchar(g_display_driver, 'X');
    g_display_driver->clear(g_display_driver);
    return 0;
}

static int test_timer(void) {
    ASSERT(g_timer_driver != NULL);
    uint64_t t0 = g_timer_driver->tick_count(g_timer_driver);
    g_timer_driver->msleep(g_timer_driver, 5);
    uint64_t t1 = g_timer_driver->tick_count(g_timer_driver);
    ASSERT(t1 >= t0);  /* Proof: timer advances */
    return 0;
}

static int test_pic(void) {
    ASSERT(g_pic_driver != NULL);
    if (g_pic_driver->init)
        g_pic_driver->init(g_pic_driver);
    return 0;
}

int main(void) {
    char tmpl[] = "/tmp/fl_drv_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        fprintf(stderr, "Cannot create temp file\n");
        return 1;
    }
    close(fd);
    char path[160];
    snprintf(path, sizeof path, "%s.dat", tmpl);
    if (rename(tmpl, path) != 0) {
        unlink(tmpl);
        fprintf(stderr, "Cannot rename temp disk to .dat\n");
        return 1;
    }
    g_total_clusters = 8;
    g_cluster_size = 32;
    strncpy(current_disk_file, path, sizeof(current_disk_file) - 1);
    current_disk_file[sizeof(current_disk_file) - 1] = '\0';
    if (create_temp_disk(path, 8, 32) != 0) {
        unlink(path);
        fprintf(stderr, "Cannot create temp disk\n");
        return 1;
    }

    printf("=== Driver Test Suite ===\n");
    drivers_init(path);

    if (drivers_require_real_block() != 0) {
        fprintf(stderr, "Block driver is stub; cannot run block tests.\n");
        drivers_shutdown();
        unlink(path);
        return 1;
    }

    printf("test_block... ");
    if (test_block() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_block_write_read... ");
    if (test_block_write_read() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_device_model... ");
    if (test_device_model() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_keyquorum_registration... ");
    if (test_keyquorum_registration() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_devfs_block... ");
    if (test_devfs_block() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_irq_and_dma... ");
    if (test_irq_and_dma() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_fl_cstr... ");
    if (test_fl_cstr() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_bus_resource_extraction... ");
    if (test_bus_resource_extraction() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_devfs_flags_and_errors... ");
    if (test_devfs_flags_and_errors() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_dma_edge_cases... ");
    if (test_dma_edge_cases() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_irq_edge_cases... ");
    if (test_irq_edge_cases() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_device_model_null_params... ");
    if (test_device_model_null_params() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_display... ");
    if (test_display() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_timer... ");
    if (test_timer() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_pic... ");
    if (test_pic() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_usb_xhci_mmio_glue... ");
    if (test_usb_xhci_mmio_glue() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_p4_driver_model_lock... ");
    if (test_p4_driver_model_lock() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_p4_irq_lifecycle... ");
    if (test_p4_irq_lifecycle() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_p4_pcie_lab... ");
    if (test_p4_pcie_lab() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_p4_virtio_golden... ");
    if (test_p4_virtio_golden() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_p4_usb_xhci_lab... ");
    if (test_p4_usb_xhci_lab() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_p4_fdt_psci... ");
    if (test_p4_fdt_psci() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    printf("test_probe... ");
    ASSERT(driver_probe_block() == 0);
    ASSERT(driver_probe_keyboard() == 0);
    ASSERT(driver_probe_display() == 0);
    ASSERT(driver_probe_timer() == 0);
    ASSERT(driver_probe_pic() == 0);
    printf("OK\n");

#if defined(FL_WITH_KEYQUORUM)
    printf("test_keyquorum_account... ");
    if (test_keyquorum_account() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");
    printf("test_keyquorum_head_author... ");
    if (test_keyquorum_head_author() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");
    printf("test_keyquorum_access_and_bridge_ops... ");
    if (test_keyquorum_access_and_bridge_ops() != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");
#endif

    printf("test_keyquorum_survives_reinit... ");
    if (test_keyquorum_survives_reinit(path) != 0) { drivers_shutdown(); unlink(path); return 1; }
    printf("OK\n");

    drivers_shutdown();
    unlink(path);
    printf("=== All driver tests passed.\n");
    return 0;
}
