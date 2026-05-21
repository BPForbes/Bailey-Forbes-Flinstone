#include "fs_service_glue.h"
#include "fl/session.h"

void fl_session_sync_services(void) {
    if (g_fm_service)
        fm_service_set_user(g_fm_service, fl_session_current_user());
}
