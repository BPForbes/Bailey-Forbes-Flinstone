#ifndef FL_FREESTANDING_KQ_DISK_H
#define FL_FREESTANDING_KQ_DISK_H
/* A persistent record log on the image's second IDE disk (the data disk, QEMU `-drive ... index=2`).
 * Records are appended; the newest record for a name wins. A blank or absent disk means no persistence,
 * and the image still runs. Each record: a header sector (magic, kind, name, length, CRC-32 over name and
 * data, written LAST as the commit point), then the data in whole sectors. A torn or damaged record ends
 * the log at that point; nothing after it is read. */

#include <stddef.h>
#include <stdint.h>

#define FL_DISK_KIND_CONTAINER 1 /* a KeyQuorum history container: name = its store path, data = .kqtf bytes */
#define FL_DISK_KIND_ACCOUNT 2   /* a sealed account key: name = account name, data = sealed key + public key */
#define FL_DISK_NAME_MAX 128u
#define FL_DISK_DATA_MAX (64u * 1024u)

typedef void (*fl_disk_apply_fn)(void *ctx, uint8_t kind, const char *name, const uint8_t *data, size_t len);

/* 0 when a data disk answers IDENTIFY (ATA PIO, secondary master), -1 otherwise. */
int fl_fs_disk_init(void);
int fl_fs_disk_present(void);
/* Calls apply for every intact record in order; returns how many, or -1 without a disk. */
int fl_fs_disk_load(fl_disk_apply_fn apply, void *ctx);
/* 0 once the record is on the disk (header last); -1 when there is no disk or no room. */
int fl_fs_disk_append(uint8_t kind, const char *name, const uint8_t *data, size_t len);

#endif
