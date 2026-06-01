#include "server_shared_fs.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    if (fl_server_shared_path_is_expired_quarantine("server_shared/expired/blocked.txt") != 1) {
        fprintf(stderr, "expected quarantine path deny\n");
        return 1;
    }
    if (fl_server_shared_path_is_expired_quarantine("server_shared/joke.txt") != 0) {
        fprintf(stderr, "expected normal shared path allowed\n");
        return 1;
    }
    printf("server_shared quarantine path checks passed.\n");
    return 0;
}
