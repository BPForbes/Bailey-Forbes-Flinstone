#include "fs_service_glue.h"
#include "fs_provider.h"
#include "fs_events.h"
#include "vrt.h"
#include <stdlib.h>

file_manager_service_t *g_fm_service = NULL;

void fs_service_glue_init(void) {
    vrt_init();
    fs_events_init();
    fs_provider_t *provider = fs_local_provider_create();
    if (provider)
        g_fm_service = fm_service_create(provider);
}

void fs_service_glue_shutdown(void) {
    if (g_fm_service) {
        fm_service_destroy(g_fm_service);
        g_fm_service = NULL;
    }
    fs_events_shutdown();
    vrt_shutdown();
}
