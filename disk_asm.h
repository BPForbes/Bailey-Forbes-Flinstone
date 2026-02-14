#ifndef DISK_ASM_H
#define DISK_ASM_H

#include "common.h"

/* ASM-backed cluster buffer I/O - hot path uses asm_mem_copy/asm_mem_zero */
int disk_asm_read_cluster(int clu_index, unsigned char *buf);
int disk_asm_write_cluster(int clu_index, const unsigned char *buf);
int disk_asm_zero_cluster(int clu_index);

#endif /* DISK_ASM_H */
