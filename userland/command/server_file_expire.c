#include "server_file_expire.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int m)
{
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12)
        return 0;
    if (m == 2 && is_leap(y))
        return 29;
    return dim[m - 1];
}

static int parse_datetime_utc(const char *arg, uint64_t *expires_at_out)
{
    int mm = 0;
    int dd = 0;
    int yyyy = 0;
    int hh = 0;
    int mi = 0;
    int ss = 0;
    char rest[8];
    int n;

    if (!arg || !expires_at_out)
        return -1;

    rest[0] = '\0';
    n = sscanf(arg, "%d-%d-%d %d:%d:%d %7s", &mm, &dd, &yyyy, &hh, &mi, &ss, rest);
    if (n >= 3 && n <= 6) {
        if (n == 3) {
            hh = 0;
            mi = 0;
            ss = 0;
        } else if (n == 5) {
            ss = 0;
        } else if (n == 6 && rest[0] != '\0') {
            return -1;
        }
    } else {
        n = sscanf(arg, "%d-%d-%d %7s", &mm, &dd, &yyyy, rest);
        if (n != 3 || rest[0] != '\0')
            return -1;
        hh = 0;
        mi = 0;
        ss = 0;
    }

    if (mm < 1 || mm > 12 || dd < 1 || dd > days_in_month(yyyy, mm))
        return -1;
    if (hh < 0 || hh > 23 || mi < 0 || mi > 59 || ss < 0 || ss > 59)
        return -1;

    {
        struct tm tmv;
        time_t t;
        memset(&tmv, 0, sizeof(tmv));
        tmv.tm_year = yyyy - 1900;
        tmv.tm_mon = mm - 1;
        tmv.tm_mday = dd;
        tmv.tm_hour = hh;
        tmv.tm_min = mi;
        tmv.tm_sec = ss;
#if defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE) || defined(_GNU_SOURCE)
        t = timegm(&tmv);
#else
        {
            char tzbuf[64];
            const char *old_tz = getenv("TZ");
            if (old_tz)
                strncpy(tzbuf, old_tz, sizeof(tzbuf) - 1u);
            tzbuf[sizeof(tzbuf) - 1u] = '\0';
            setenv("TZ", "UTC0", 1);
            tzset();
            t = mktime(&tmv);
            if (old_tz)
                setenv("TZ", tzbuf, 1);
            else
                unsetenv("TZ");
            tzset();
        }
#endif
        if (t < 0)
            return -1;
        *expires_at_out = (uint64_t)t;
        return 0;
    }
}

static int parse_duration(const char *arg, uint64_t *expires_at_out)
{
    unsigned long n = 0;
    char unit = '\0';

    if (!arg || !expires_at_out)
        return -1;
    if (sscanf(arg, "%lu%c", &n, &unit) < 1)
        return -1;
    *expires_at_out = (uint64_t)time(NULL);
    switch (unit) {
    case 's': case 'S':
        *expires_at_out += (uint64_t)n;
        break;
    case 'h': case 'H':
        *expires_at_out += (uint64_t)n * 3600u;
        break;
    case 'm': case 'M':
        *expires_at_out += (uint64_t)n * 60u;
        break;
    case 'd': case 'D':
        *expires_at_out += (uint64_t)n * 86400u;
        break;
    case '\0':
        *expires_at_out += (uint64_t)n;
        break;
    default:
        return -1;
    }
    return 0;
}

int fl_server_file_parse_expire_arg(const char *arg, uint64_t *expires_at_out)
{
    if (!arg || !*arg || !expires_at_out)
        return -1;
    if (strchr(arg, '-'))
        return parse_datetime_utc(arg, expires_at_out);
    return parse_duration(arg, expires_at_out);
}

void fl_server_file_format_expires_utc(uint64_t expires_at, char *out, size_t out_cap)
{
    time_t t = (time_t)expires_at;
    struct tm tmv;
    if (!out || out_cap == 0u)
        return;
    out[0] = '\0';
    if (expires_at == 0u)
        return;
    if (!gmtime_r(&t, &tmv)) {
        snprintf(out, out_cap, "unknown");
        return;
    }
    snprintf(out, out_cap, "%02d-%02d-%04d %02d:%02d:%02d UTC",
             tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_year + 1900,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}
