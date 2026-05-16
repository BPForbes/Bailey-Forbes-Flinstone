#include "fl/ring_log.h"
#include <pthread.h>
#include <string.h>

static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static char s_buf[FL_RING_LOG_CAPACITY];
static size_t s_len;
static unsigned s_drops;

void fl_ring_log_reset(void) {
    pthread_mutex_lock(&s_mu);
    s_len = 0;
    s_buf[0] = '\0';
    s_drops = 0;
    pthread_mutex_unlock(&s_mu);
}

unsigned fl_ring_log_drop_count(void) {
    pthread_mutex_lock(&s_mu);
    unsigned d = s_drops;
    pthread_mutex_unlock(&s_mu);
    return d;
}

void fl_ring_log_append_line(const char *line) {
    if (!line)
        return;
    size_t ln = strlen(line);
    if (ln == 0u)
        return;
    size_t add = ln + 1u;
    if (add > FL_RING_LOG_CAPACITY) {
        pthread_mutex_lock(&s_mu);
        s_drops++;
        pthread_mutex_unlock(&s_mu);
        return;
    }

    pthread_mutex_lock(&s_mu);
    while (s_len + add > FL_RING_LOG_CAPACITY && s_len > 0u) {
        const char *nl = memchr(s_buf, '\n', s_len);
        if (!nl) {
            s_len = 0;
            s_buf[0] = '\0';
            break;
        }
        size_t skip = (size_t)(nl - s_buf) + 1u;
        memmove(s_buf, s_buf + skip, s_len - skip);
        s_len -= skip;
        s_drops++;
    }
    if (s_len + add > FL_RING_LOG_CAPACITY) {
        s_drops++;
        pthread_mutex_unlock(&s_mu);
        return;
    }
    memcpy(s_buf + s_len, line, ln);
    s_len += ln;
    s_buf[s_len++] = '\n';
    s_buf[s_len] = '\0';
    pthread_mutex_unlock(&s_mu);
}

size_t fl_ring_log_copy_out(char *buf, size_t cap) {
    if (!buf || cap < 2u)
        return 0u;
    pthread_mutex_lock(&s_mu);
    size_t n = s_len;
    if (n >= cap)
        n = cap - 1u;
    memcpy(buf, s_buf, n);
    buf[n] = '\0';
    pthread_mutex_unlock(&s_mu);
    return n;
}
