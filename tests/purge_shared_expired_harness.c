#include "server_shared_fs.h"

#include <stdio.h>
#include <time.h>

int main(void)
{
    if (fl_server_shared_purge_expired((uint64_t)time(NULL)) != FL_RESULT_OK) {
        fprintf(stderr, "fl_server_shared_purge_expired failed\n");
        return 1;
    }
    return 0;
}
