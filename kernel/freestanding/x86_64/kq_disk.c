/* The data disk: ATA PIO on the secondary master (I/O 0x170, control 0x376), LBA28, one sector per command.
 * Polled, interrupts off, every wait bounded, so a missing or stuck disk ends the call rather than the boot. */
#include "kq_disk.h"

#include "ioport.h"
#include "mem_domain.h"

#include <string.h>

#define ATA_IO 0x170u
#define ATA_CTL 0x376u
#define REG_DATA 0u
#define REG_COUNT 2u
#define REG_LBA0 3u
#define REG_LBA1 4u
#define REG_LBA2 5u
#define REG_DRIVE 6u
#define REG_STATUS 7u
#define ST_ERR 0x01u
#define ST_DF 0x20u
#define ST_DRQ 0x08u
#define ST_BSY 0x80u
#define CMD_READ 0x20u
#define CMD_WRITE 0x30u
#define CMD_FLUSH 0xE7u
#define CMD_IDENTIFY 0xECu
/* Bound every ATA poll: 4e6 inb loops is fine on native hardware, but under Wasm QEMU each
 * port I/O is a TCG trap. A floating secondary (browser lab has no index=2 disk) often reads
 * status 0xFF with BSY set; burning millions of polls there stalls past the 90s Ready gate. */
#define POLL_MAX 100000u
#define SECTOR 512u
#define REC_MAGIC 0x4b514c47u /* "GLQK" in memory: KeyQuorum log */
#define HDR_NAME 16u

static uint32_t s_sectors;
static uint32_t s_next;
static int s_present;

static uint16_t inw_port(uint16_t port) {
    uint16_t v;
    __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

static void delay400(void) {
    for (int i = 0; i < 4; i++)
        (void)fl_fs_inb((uint16_t)ATA_CTL);
}

/* Status once BSY clears, or 0xFF on a floating bus (no drive); -1 on timeout. */
static int wait_idle(void) {
    for (uint32_t i = 0; i < POLL_MAX; i++) {
        uint8_t s = fl_fs_inb((uint16_t)(ATA_IO + REG_STATUS));
        if (s == 0xFFu) /* floating bus: every bit reads high; do not treat BSY as busy forever */
            return 0xFF;
        if (!(s & ST_BSY))
            return s;
    }
    return -1;
}

static int wait_drq(void) {
    for (uint32_t i = 0; i < POLL_MAX; i++) {
        uint8_t s = fl_fs_inb((uint16_t)(ATA_IO + REG_STATUS));
        if (s == 0xFFu)
            return -1;
        if (s & (ST_ERR | ST_DF))
            return -1;
        if (!(s & ST_BSY) && (s & ST_DRQ))
            return 0;
    }
    return -1;
}

static int identify(void) {
    uint16_t id[256];
    int s;
    uint8_t st;

    fl_fs_outb((uint16_t)(ATA_IO + REG_DRIVE), 0xA0);
    delay400();
    fl_fs_outb((uint16_t)(ATA_IO + REG_COUNT), 0);
    fl_fs_outb((uint16_t)(ATA_IO + REG_LBA0), 0);
    fl_fs_outb((uint16_t)(ATA_IO + REG_LBA1), 0);
    fl_fs_outb((uint16_t)(ATA_IO + REG_LBA2), 0);
    fl_fs_outb((uint16_t)(ATA_IO + REG_STATUS), CMD_IDENTIFY);
    delay400();
    st = fl_fs_inb((uint16_t)(ATA_IO + REG_STATUS));
    if (st == 0 || st == 0xFFu)
        return -1; /* no device, or floating bus (browser lab: no secondary disk) */
    s = wait_idle();
    if (s < 0 || s == 0xFF)
        return -1;
    /* A packet device (ATAPI) reports a signature in LBA1/LBA2 instead of an error: not a data disk. */
    if (fl_fs_inb((uint16_t)(ATA_IO + REG_LBA1)) || fl_fs_inb((uint16_t)(ATA_IO + REG_LBA2)))
        return -1;
    if (wait_drq() != 0)
        return -1;
    for (unsigned i = 0; i < 256; i++)
        id[i] = inw_port((uint16_t)ATA_IO);
    s_sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    return s_sectors ? 0 : -1;
}

static int read_sector(uint32_t lba, uint8_t *buf) {
    int s;

    if (lba >= s_sectors || lba >= (1u << 28))
        return -1;
    if (wait_idle() < 0)
        return -1;
    fl_fs_outb((uint16_t)(ATA_IO + REG_DRIVE), (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu)));
    fl_fs_outb((uint16_t)(ATA_IO + REG_COUNT), 1);
    fl_fs_outb((uint16_t)(ATA_IO + REG_LBA0), (uint8_t)lba);
    fl_fs_outb((uint16_t)(ATA_IO + REG_LBA1), (uint8_t)(lba >> 8));
    fl_fs_outb((uint16_t)(ATA_IO + REG_LBA2), (uint8_t)(lba >> 16));
    fl_fs_outb((uint16_t)(ATA_IO + REG_STATUS), CMD_READ);
    delay400();
    if (wait_drq() != 0)
        return -1;
    for (unsigned w = 0; w < SECTOR / 2; w++) {
        uint16_t v = inw_port((uint16_t)ATA_IO);
        buf[2 * w] = (uint8_t)v;
        buf[2 * w + 1] = (uint8_t)(v >> 8);
    }
    s = fl_fs_inb((uint16_t)(ATA_IO + REG_STATUS));
    return (s & (ST_ERR | ST_DF)) ? -1 : 0;
}

static int write_sector(uint32_t lba, const uint8_t *buf) {
    int s;

    if (lba >= s_sectors || lba >= (1u << 28))
        return -1;
    if (wait_idle() < 0)
        return -1;
    fl_fs_outb((uint16_t)(ATA_IO + REG_DRIVE), (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu)));
    fl_fs_outb((uint16_t)(ATA_IO + REG_COUNT), 1);
    fl_fs_outb((uint16_t)(ATA_IO + REG_LBA0), (uint8_t)lba);
    fl_fs_outb((uint16_t)(ATA_IO + REG_LBA1), (uint8_t)(lba >> 8));
    fl_fs_outb((uint16_t)(ATA_IO + REG_LBA2), (uint8_t)(lba >> 16));
    fl_fs_outb((uint16_t)(ATA_IO + REG_STATUS), CMD_WRITE);
    delay400();
    if (wait_drq() != 0)
        return -1;
    for (unsigned w = 0; w < SECTOR / 2; w++)
        __asm__ volatile("outw %0,%1" : : "a"((uint16_t)(buf[2 * w] | (buf[2 * w + 1] << 8))), "Nd"((uint16_t)ATA_IO));
    fl_fs_outb((uint16_t)(ATA_IO + REG_STATUS), CMD_FLUSH);
    if (wait_idle() < 0)
        return -1;
    s = fl_fs_inb((uint16_t)(ATA_IO + REG_STATUS));
    return (s & (ST_ERR | ST_DF)) ? -1 : 0;
}

/* CRC-32 (reflected, poly 0xEDB88320), bitwise: a few dozen bytes of code and no table. */
static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t sectors_for(size_t len) {
    return (uint32_t)((len + SECTOR - 1) / SECTOR);
}

int fl_fs_disk_init(void) {
    s_present = identify() == 0;
    s_next = 0;
    return s_present ? 0 : -1;
}

int fl_fs_disk_present(void) {
    return s_present;
}

int fl_fs_disk_load(fl_disk_apply_fn apply, void *ctx) {
    uint8_t hdr[SECTOR];
    int count = 0;

    if (!s_present)
        return -1;
    s_next = 0;
    while (s_next < s_sectors && read_sector(s_next, hdr) == 0 && rd32(hdr) == REC_MAGIC) {
        uint8_t kind = hdr[4];
        uint16_t nl = (uint16_t)(hdr[6] | (hdr[7] << 8));
        uint32_t dl = rd32(hdr + 8), crc = rd32(hdr + 12);
        uint32_t psec = sectors_for(dl), need = 1 + psec;
        char name[FL_DISK_NAME_MAX + 1];
        uint8_t *data;
        uint32_t i;
        int ok = 1;

        if ((kind != FL_DISK_KIND_CONTAINER && kind != FL_DISK_KIND_ACCOUNT) || nl == 0 || nl > FL_DISK_NAME_MAX ||
            dl > FL_DISK_DATA_MAX || (uint32_t)s_next + need > s_sectors)
            break;
        memcpy(name, hdr + HDR_NAME, nl);
        name[nl] = '\0';
        data = (uint8_t *)mem_domain_alloc(MEM_DOMAIN_FS, dl ? dl : 1);
        if (!data)
            break;
        for (i = 0; i < psec && ok; i++) {
            uint8_t sec[SECTOR];
            size_t off = (size_t)i * SECTOR, take = dl - off < SECTOR ? dl - off : SECTOR;

            ok = read_sector(s_next + 1 + i, sec) == 0;
            if (ok)
                memcpy(data + off, sec, take);
        }
        if (ok && (crc_update(crc_update(0, (const uint8_t *)name, nl), data, dl) != crc))
            ok = 0;
        if (!ok) {
            mem_domain_free(MEM_DOMAIN_FS, data);
            break; /* a torn or damaged record ends the log */
        }
        apply(ctx, kind, name, data, dl);
        mem_domain_free(MEM_DOMAIN_FS, data);
        s_next += need;
        count++;
    }
    return count;
}

int fl_fs_disk_append(uint8_t kind, const char *name, const uint8_t *data, size_t len) {
    uint8_t hdr[SECTOR];
    size_t nl;
    uint32_t psec, i;
    uint32_t crc;

    if (!s_present || !name || (!data && len) || len > FL_DISK_DATA_MAX)
        return -1;
    nl = strlen(name);
    if (nl == 0 || nl > FL_DISK_NAME_MAX || (kind != FL_DISK_KIND_CONTAINER && kind != FL_DISK_KIND_ACCOUNT))
        return -1;
    psec = sectors_for(len);
    if ((uint32_t)s_next + 1 + psec > s_sectors)
        return -1;
    /* The data first, the header last: a power cut before the header leaves no record. */
    for (i = 0; i < psec; i++) {
        uint8_t sec[SECTOR];
        size_t off = (size_t)i * SECTOR, take = len - off < SECTOR ? len - off : SECTOR;

        memset(sec, 0, sizeof sec);
        memcpy(sec, data + off, take);
        if (write_sector(s_next + 1 + i, sec) != 0)
            return -1;
    }
    crc = crc_update(crc_update(0, (const uint8_t *)name, nl), data ? data : (const uint8_t *)"", len);
    memset(hdr, 0, sizeof hdr);
    wr32(hdr, REC_MAGIC);
    hdr[4] = kind;
    hdr[6] = (uint8_t)nl;
    hdr[7] = (uint8_t)(nl >> 8);
    wr32(hdr + 8, (uint32_t)len);
    wr32(hdr + 12, crc);
    memcpy(hdr + HDR_NAME, name, nl);
    if (write_sector(s_next, hdr) != 0)
        return -1;
    s_next += 1 + psec;
    return 0;
}
