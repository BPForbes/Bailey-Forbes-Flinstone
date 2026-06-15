#include "net_wifi_host_linux.h"
#include "net_wifi_station.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ASSERT(c)                                                              \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int write_fake_helper(char *path, size_t cap) {
    char dir_template[] = "/tmp/fl helper XXXXXX";
    char *dir = mkdtemp(dir_template);
    FILE *fp;

    if (!dir)
        return 0;
    snprintf(path, cap, "%s/FlinstonePowershell.exe", dir);
    fp = fopen(path, "w");
    if (!fp)
        return 0;
    fputs("#!/bin/sh\n"
          "case \"$1\" in\n"
          "  wifi-scan)\n"
          "    printf 'ssid=RouterLan\\tbssid=02:aa:bb:cc:dd:ee\\trssi=-41\\tauth=wpa3\\tband=5\\tchan=36\\n'\n"
          "    ;;\n"
          "  wifi-status)\n"
          "    printf 'state=connected\\tssid=RouterLan\\tipv4=192.168.1.235\\tprefix=24\\tgateway=192.168.1.1\\n'\n"
          "    ;;\n"
          "  *)\n"
          "    printf 'result=ok\\n'\n"
          "    ;;\n"
          "esac\n",
          fp);
    fclose(fp);
    return chmod(path, 0700) == 0;
}

int main(void) {
    char helper_path[256];
    fl_net_wifi_scan_entry_t entries[4];
    size_t count = 0;
    const char *win_ip;

    ASSERT(write_fake_helper(helper_path, sizeof(helper_path)));
    ASSERT(setenv("FL_NET_WIFI_FLINSTONE_PS", helper_path, 1) == 0);
    unsetenv("FL_NET_WIFI_USE_WPA");

    ASSERT(fl_net_wifi_station_init() == FL_RESULT_OK);
    ASSERT(fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_scan_result(entries, 4, &count) == FL_RESULT_OK);
    ASSERT(count == 1u);
    ASSERT(strcmp(entries[0].ssid, "RouterLan") == 0);
    ASSERT(entries[0].auth_mode == FL_WIFI_AUTH_WPA3_SAE);
    ASSERT(entries[0].band == FL_WIFI_BAND_5GHZ);

    ASSERT(strcmp(fl_net_wifi_host_linux_backend_name(), "FlinstonePowershell") == 0);
    win_ip = fl_net_wifi_host_linux_windows_ipv4();
    ASSERT(win_ip != NULL);
    ASSERT(strcmp(win_ip, "192.168.1.235") == 0);

    puts("test_wifi_flinstone_helper: helper discovery/status... ok");
    return 0;
}
