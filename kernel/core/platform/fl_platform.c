#include "fl_platform.h"

#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <stdio.h>
#endif

static fl_platform_type_t s_platform = FL_PLATFORM_UNDETECTED;

fl_platform_type_t fl_platform_detect(void) {
    if (s_platform != FL_PLATFORM_UNDETECTED)
        return s_platform;

#if defined(__APPLE__)
    s_platform = FL_PLATFORM_MACOS;

#elif defined(__linux__)
    /*
     * WSL detection (both WSL1 and WSL2):
     *   1. /proc/version contains "Microsoft" or "WSL"
     *   2. WSL_DISTRO_NAME env var is set by the WSL launcher
     */
    {
        FILE *f = fopen("/proc/version", "r");
        if (f) {
            char buf[256];
            size_t n = fread(buf, 1u, sizeof(buf) - 1u, f);
            buf[n] = '\0';
            fclose(f);
            if (strstr(buf, "Microsoft") || strstr(buf, "WSL")) {
                s_platform = FL_PLATFORM_WSL;
                return s_platform;
            }
        }
    }
    {
        const char *v = getenv("WSL_DISTRO_NAME");
        if (v && v[0]) {
            s_platform = FL_PLATFORM_WSL;
            return s_platform;
        }
    }
    s_platform = FL_PLATFORM_LINUX;

#else
    s_platform = FL_PLATFORM_UNKNOWN;
#endif

    return s_platform;
}

const char *fl_platform_name(fl_platform_type_t t) {
    switch (t) {
    case FL_PLATFORM_WSL:   return "wsl";
    case FL_PLATFORM_LINUX: return "linux";
    case FL_PLATFORM_MACOS: return "macos";
    default:                return "unknown";
    }
}
