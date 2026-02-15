/* Virtual disk: raw sector file. ASM for buffer ops. */
#include "vm_disk.h"
#include "mem_asm.h"
#include "mem_domain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR_SIZE 512

static FILE *s_vm_disk_fp;
static uint32_t s_vm_disk_sectors;

int vm_disk_init(const char *path, unsigned int size_mb) {
    if (!path || size_mb == 0) return -1;
    vm_disk_shutdown();
    s_vm_disk_fp = fopen(path, "r+b");
    if (!s_vm_disk_fp)
        s_vm_disk_fp = fopen(path, "w+b");
    if (!s_vm_disk_fp) return -1;
    size_t target = (size_t)size_mb * 1024 * 1024;
    if (fseek(s_vm_disk_fp, 0, SEEK_END) != 0) { fclose(s_vm_disk_fp); s_vm_disk_fp = NULL; return -1; }
    long pos = ftell(s_vm_disk_fp);
    if (pos < 0) { fclose(s_vm_disk_fp); s_vm_disk_fp = NULL; return -1; }
    if ((size_t)pos < target) {
        void *zeros = mem_domain_alloc(MEM_DOMAIN_FS, 65536);
        if (!zeros) { fclose(s_vm_disk_fp); s_vm_disk_fp = NULL; return -1; }
        asm_mem_zero(zeros, 65536);
        for (size_t n = (size_t)pos; n < target; n += 65536) {
            size_t chunk = (target - n) < 65536 ? (target - n) : 65536;
            if (fwrite(zeros, 1, chunk, s_vm_disk_fp) != chunk) {
                mem_domain_free(MEM_DOMAIN_FS, zeros);
                fclose(s_vm_disk_fp);
                s_vm_disk_fp = NULL;
                return -1;
            }
        }
        mem_domain_free(MEM_DOMAIN_FS, zeros);
        fflush(s_vm_disk_fp);
    }
    s_vm_disk_sectors = (uint32_t)(target / SECTOR_SIZE);
    return 0;
}

void vm_disk_shutdown(void) {
    if (s_vm_disk_fp) {
        fclose(s_vm_disk_fp);
        s_vm_disk_fp = NULL;
    }
    s_vm_disk_sectors = 0;
}

int vm_disk_read_sector(uint32_t lba, void *buf) {
    if (!s_vm_disk_fp || !buf || lba >= s_vm_disk_sectors) return -1;
    if (fseek(s_vm_disk_fp, (long)lba * SECTOR_SIZE, SEEK_SET) != 0) return -1;
    if (fread(buf, 1, SECTOR_SIZE, s_vm_disk_fp) != SECTOR_SIZE) return -1;
    return 0;
}

int vm_disk_write_sector(uint32_t lba, const void *buf) {
    if (!s_vm_disk_fp || !buf || lba >= s_vm_disk_sectors) return -1;
    if (fseek(s_vm_disk_fp, (long)lba * SECTOR_SIZE, SEEK_SET) != 0) return -1;
    if (fwrite(buf, 1, SECTOR_SIZE, s_vm_disk_fp) != SECTOR_SIZE) return -1;
    fflush(s_vm_disk_fp);
    return 0;
}

int vm_disk_is_active(void) {
    return s_vm_disk_fp != NULL;
}
