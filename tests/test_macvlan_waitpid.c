#include "net_macvlan.h"

#include <errno.h>
#include <stdio.h>

#define ASSERT(c)                                                              \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int test_waitpid_classify(void)
{
    const long child = 4242;

    ASSERT(fl_net_macvlan_waitpid_classify(child, child, 0) == 1);
    ASSERT(fl_net_macvlan_waitpid_classify(child, child, EINTR) == 1);
    ASSERT(fl_net_macvlan_waitpid_classify(child, -1, EINTR) == 0);
    ASSERT(fl_net_macvlan_waitpid_classify(child, -1, ECHILD) == -1);
    ASSERT(fl_net_macvlan_waitpid_classify(child, 0, 0) == -1);
    ASSERT(fl_net_macvlan_waitpid_classify(child, child + 1, 0) == -1);
    ASSERT(fl_net_macvlan_waitpid_classify(0, 0, 0) == -1);
    ASSERT(fl_net_macvlan_waitpid_classify(-1, -1, EINTR) == -1);
    return 0;
}

int main(void)
{
    if (test_waitpid_classify() != 0)
        return 1;
    puts("test_macvlan_waitpid: classify reaped / EINTR / error passed");
    return 0;
}
