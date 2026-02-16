#ifndef FS_SERVICE_GLUE_H
#define FS_SERVICE_GLUE_H

#include "fs_facade.h"

extern file_manager_service_t *g_fm_service;

void fs_service_glue_init(void);
void fs_service_glue_shutdown(void);

#endif /* FS_SERVICE_GLUE_H */
