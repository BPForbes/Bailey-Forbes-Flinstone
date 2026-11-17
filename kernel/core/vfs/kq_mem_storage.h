#ifndef FL_KQ_MEM_STORAGE_H
#define FL_KQ_MEM_STORAGE_H

#include "kq_storage.h"

#include <stddef.h>

#define FL_KQ_MEM_FILES_MAX 32u
#define FL_KQ_MEM_PATH_MAX 192u
#define FL_KQ_MEM_DIRS_MAX 8u
#ifndef FL_KQ_MEM_BYTES_MAX
#define FL_KQ_MEM_BYTES_MAX (4u * 1024u * 1024u)
#endif

const fl_kq_storage_t *fl_kq_storage_memory(void);

void fl_kq_storage_memory_reset(void);

size_t fl_kq_storage_memory_bytes(void);
size_t fl_kq_storage_memory_files(void);

void fl_kq_storage_memory_set_limit(size_t bytes);

#endif
