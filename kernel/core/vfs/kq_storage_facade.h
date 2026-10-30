#ifndef FL_KQ_STORAGE_FACADE_H
#define FL_KQ_STORAGE_FACADE_H

/*
 * The KeyQuorum checkpoints' storage table (kq_storage.h) over the file manager facade, for the
 * native shell: every checkpoint read and write then goes through fm_read_bytes,
 * fm_write_atomic and the fm_dir_* calls, so the policy, the write chain, the save events and
 * the path log see it, over the provider's contained slots. The lab does not link this file.
 */

#include "fs_facade.h"
#include "kq_storage.h"

/**
 * Fills *out with a table over svc and returns it (NULL for a NULL svc or out). *out must
 * outlive its use, as it is what fl_kq_cp_set_storage keeps. One existence check stays on the
 * placement layer: the facade has no existence call, and "is a container there" reveals no
 * content.
 */
const fl_kq_storage_t *fl_kq_storage_facade(file_manager_service_t *svc, fl_kq_storage_t *out);

#endif /* FL_KQ_STORAGE_FACADE_H */
