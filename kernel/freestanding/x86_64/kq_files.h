#ifndef FL_FREESTANDING_KQ_FILES_H
#define FL_FREESTANDING_KQ_FILES_H
/* KeyQuorum file histories in the freestanding image (FL-02 stage 2): the checkpoints over the in-memory
 * storage table, signed with the account keys kq_identity.c seals. Not in the security-lab image. */

#include "kq_keydir.h"

#include <stdint.h>

/* kq_identity.c: the account keys. */
void fl_fs_kq_bind_identity(void);
const fl_kq_keydir_t *fl_fs_kq_keydir(void);
/* 0 with the account's signing secret in sec (the caller wipes it), or -1 for a wrong password. */
int fl_fs_kq_open_signing(const char *name, const char *password, uint8_t sec[32]);

/* kq_platform.c: the CMOS clock as KeyQuorum's UTC text, "YYYY-MM-DDTHH:MM:SS.000Z"; -1 without a clock. */
int fl_fs_utc_text(char out[25]);

/* kq_files.c: server file sign | log | verify, and the boot self-test. */
void fl_fs_kq_files_init(void (*emit)(const char *line));
int fl_fs_kq_file_sign(int session, const char *path, const char *password);
int fl_fs_kq_file_log(int session, const char *path);
int fl_fs_kq_file_verify(int session, const char *path);
/* 0 when a history can be started, signed, trusted and refused under a wrong key; leaves the store empty. */
int fl_fs_kq_selftest(void);

#endif
