#ifndef FL_FREESTANDING_FWCFG_H
#define FL_FREESTANDING_FWCFG_H

#include <stddef.h>
#include <stdint.h>

/*
 * QEMU's fw_cfg device (selector port 0x510, data port 0x511): named files the launcher hands the guest with
 * -fw_cfg name=...,file=.... This driver only reads them; what a file means (entropy, here) is the caller's.
 */

/** 1 when the device answers with QEMU's signature, else 0 (a machine with no fw_cfg reads 0xFF). */
int fl_fs_fwcfg_present(void);

/**
 * Copies len bytes of the file `name` starting at byte `offset` into out. 0 on success; -1 when there is no
 * device, no such file, or the file has fewer than offset + len bytes (nothing is written to out then).
 * `name` is at most 55 characters (fw_cfg's limit).
 */
int fl_fs_fwcfg_read(const char *name, uint32_t offset, uint8_t *out, size_t len);

#endif
