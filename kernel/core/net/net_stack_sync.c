#include "net_stack_sync.h"

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#define FL_NET_STACK_SYNC_HOSTED 1
#include <pthread.h>
#endif

#if defined(FL_NET_STACK_SYNC_HOSTED)
static pthread_mutex_t s_net_stack_mu;
static int s_net_stack_inited;
#endif

void fl_net_stack_sync_init(void) {
#if defined(FL_NET_STACK_SYNC_HOSTED)
    if (s_net_stack_inited)
        return;
    {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&s_net_stack_mu, &attr);
        pthread_mutexattr_destroy(&attr);
    }
    s_net_stack_inited = 1;
#endif
}

void fl_net_stack_lock(void) {
#if defined(FL_NET_STACK_SYNC_HOSTED)
    if (!s_net_stack_inited)
        fl_net_stack_sync_init();
    pthread_mutex_lock(&s_net_stack_mu);
#endif
}

void fl_net_stack_unlock(void) {
#if defined(FL_NET_STACK_SYNC_HOSTED)
    if (!s_net_stack_inited)
        return;
    pthread_mutex_unlock(&s_net_stack_mu);
#endif
}
