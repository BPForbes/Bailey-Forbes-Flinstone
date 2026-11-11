#ifndef FL_KQ_MEM_STORAGE_H
#define FL_KQ_MEM_STORAGE_H

/*
 * An in-memory fl_kq_storage_t (FL-02 stage 2): the table the checkpoints use where there is no file system
 * for containers and sealed letters, the way the wasm lab keeps them in Emscripten's in-memory FS and
 * KeyQuorum's own lab in an in-memory Storage. Files live in MEM_DOMAIN_FS memory; nothing survives a reset.
 *
 * It uses only the allocator, memcpy, memcmp and strlen, so the freestanding image can link it. Bounded: at
 * most FL_KQ_MEM_FILES_MAX files, paths under FL_KQ_MEM_PATH_MAX, FL_KQ_MEM_BYTES_MAX bytes in all (a limit is
 * measured against the image's RAM, not chosen here). No locking: the checkpoints run under the shell's lock.
 *
 * A write replaces the old bytes only after the new ones are held, and dir_commit of n files either replaces
 * all of them or none. Results follow kq_storage.h: read 0 / -1 / -2 (larger than max) / -3 (absent), exists
 * 1 / 0, commit 0 / -1. Freed bytes are wiped.
 */

#include "kq_storage.h"

#include <stddef.h>

#define FL_KQ_MEM_FILES_MAX 32u
#define FL_KQ_MEM_PATH_MAX 192u
#define FL_KQ_MEM_DIRS_MAX 8u
#ifndef FL_KQ_MEM_BYTES_MAX
#define FL_KQ_MEM_BYTES_MAX (4u * 1024u * 1024u)
#endif

/** The one table; install it with fl_kq_cp_set_storage(). */
const fl_kq_storage_t *fl_kq_storage_memory(void);

/** Wipes and frees every file and forgets every directory. */
void fl_kq_storage_memory_reset(void);

/** Bytes held now, and files held now. */
size_t fl_kq_storage_memory_bytes(void);
size_t fl_kq_storage_memory_files(void);

/** Lowers (or restores, with FL_KQ_MEM_BYTES_MAX) the byte limit: a write that would pass it fails. For tests of
 *  the all-or-nothing commit, and for a launcher that measured less RAM. Never raises it above the compiled limit. */
void fl_kq_storage_memory_set_limit(size_t bytes);

#endif /* FL_KQ_MEM_STORAGE_H */
