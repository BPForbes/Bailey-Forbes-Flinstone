#ifndef VM_DISK_H
#define VM_DISK_H

#include <stdint.h>

#define VM_DISK_DEFAULT_SIZE_MB 2
#define VM_DISK_SECTOR_SIZE 512

/* Virtual disk: raw binary file, fixed size. Single backing file per VM. */
int vm_disk_init(const char *path, unsigned int size_mb);
void vm_disk_shutdown(void);
int vm_disk_read_sector(uint32_t lba, void *buf);
int vm_disk_write_sector(uint32_t lba, const void *buf);
int vm_disk_is_active(void);

#endif /* VM_DISK_H */
