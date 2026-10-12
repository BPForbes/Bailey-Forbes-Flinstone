#include "net_macvlan.h"

#include <errno.h>

int fl_net_macvlan_waitpid_classify(long expected, long waited, int wait_errno)
{
    if (expected <= 0)
        return -1;
    if (waited == expected)
        return 1;
    if (waited < 0 && wait_errno == EINTR)
        return 0;
    return -1;
}
