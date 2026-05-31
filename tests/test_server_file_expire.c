#include "server_file_expire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int test_duration_units(void)
{
    uint64_t exp = 0;
    uint64_t now = (uint64_t)time(NULL);
    ASSERT(fl_server_file_parse_expire_arg("2s", &exp) == 0);
    ASSERT(exp >= now + 1u && exp <= now + 3u);
    ASSERT(fl_server_file_parse_expire_arg("1h", &exp) == 0);
    ASSERT(exp >= now + 3600u - 1u);
    return 0;
}

static int test_datetime_utc(void)
{
    uint64_t exp = 0;
    ASSERT(fl_server_file_parse_expire_arg("12-31-2030", &exp) == 0);
    ASSERT(exp > 0u);
    ASSERT(fl_server_file_parse_expire_arg("06-01-2030 15:30", &exp) == 0);
    ASSERT(exp > 0u);
    ASSERT(fl_server_file_parse_expire_arg("06-01-2030 15:30:45", &exp) == 0);
    ASSERT(exp > 0u);
    return 0;
}

int main(void)
{
    if (test_duration_units() != 0)
        return 1;
    if (test_datetime_utc() != 0)
        return 1;
    printf("All server file expire tests passed.\n");
    return 0;
}
