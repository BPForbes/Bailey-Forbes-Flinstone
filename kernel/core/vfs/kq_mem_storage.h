#ifndef FL_KQ_MEM_STORAGE_H
#define FL_KQ_MEM_STORAGE_H

/*
 * In-memory fl_kq_storage_t in MEM_DOMAIN_FS memory, for images with no file system. Bounded by the
 * limits below; no locking (the caller serialises). Writes and dir_commit are all-or-nothing, freed
 * bytes are wiped, and results follow kq_storage.h.
 */

#include "kq_storage.h"

#include <stddef.h>

#define FL_KQ_MEM_FILES_MAX 32u
#define FL_KQ_MEM_PATH_MAX 192u
#define FL_KQ_MEM_DIRS_MAX 8u
#ifndef FL_KQ_MEM_BYTES_MAX
#define FL_KQ_MEM_BYTES_MAX (4u * 1024u * 1024u)
#endif

/** Returns the one table; install it with fl_kq_cp_set_storage(). */
const fl_kq_storage_t *fl_kq_storage_memory(void);

/** Wipes and frees every file and forgets every directory. */
void fl_kq_storage_memory_reset(void);

/** Return the bytes and the files held now. */
size_t fl_kq_storage_memory_bytes(void);
size_t fl_kq_storage_memory_files(void);

/** Lowers or restores the byte limit; never raises it above FL_KQ_MEM_BYTES_MAX. */
void fl_kq_storage_memory_set_limit(size_t bytes);

#endif /* FL_KQ_MEM_STORAGE_H */
