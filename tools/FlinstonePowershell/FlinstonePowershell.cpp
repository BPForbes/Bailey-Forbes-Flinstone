/*
 * FlinstonePowershell — Windows Wi-Fi bridge for Bailey-Forbes-Flinstone.
 *
 * This program is the WSL ↔ Windows bridge.  When the Flinstone shell runs
 * inside WSL it cannot reach Windows-managed Wi-Fi through wpa_cli/nmcli.
 * The C kernel backend (net_wifi_host_linux.c) spawns this executable via
 * WSL interop and reads its stdout.
 *
 * Build targets
 * -------------
 * Windows / mingw32 cross-compile (final form, full WlanAPI support):
 *   x86_64-w64-mingw32-g++ -std=c++17 -o FlinstonePowershell.exe \
 *       FlinstonePowershell.cpp -lwlanapi -lole32
 *
 * Linux / WSL (development build, uses netsh.exe via WSL interop):
 *   g++ -std=c++17 -o FlinstonePowershell FlinstonePowershell.cpp
 *
 * stdout protocol (consumed by parse_flinstone_ps_scan in net_wifi_host_linux.c)
 * -------------------------------------------------------------------------------
 * wifi-scan  : one line per network, fields tab-separated:
 *   ssid=<name>\tbssid=<aa:bb:cc:dd:ee:ff>\trssi=<dBm>\tauth=<open|wpa2|wpa3>\tband=<2.4|5|6>\tchan=<n>
 *
 * wifi-join  : single line:
 *   result=ok\tssid=<name>          on success
 *   result=error\tmsg=<reason>      on failure
 *
 * wifi-leave : single line:
 *   result=ok
 *   result=error\tmsg=<reason>
 *
 * wifi-status: single line:
 *   state=connected\tssid=<name>
 *   state=disconnected
 *
 * platform   : single line:
 *   platform=<windows|linux|macos>
 */

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/*
 * TODO (full WlanAPI implementation):
 *   #include <wlanapi.h>
 *   #pragma comment(lib, "wlanapi.lib")
 *   #pragma comment(lib, "ole32.lib")
 *
 * Replace the netsh-based stubs below with native calls:
 *   WlanOpenHandle / WlanCloseHandle
 *   WlanEnumInterfaces
 *   WlanScan + WlanGetNetworkBssList  (wifi-scan)
 *   WlanSetProfile + WlanConnect      (wifi-join)
 *   WlanDisconnect                    (wifi-leave)
 *   WlanQueryInterface                (wifi-status)
 */
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

/* Run a command, return all stdout. Only needed on Windows builds. */
#if defined(_WIN32)
static std::string capture(const char *cmd) {
    std::string out;
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return out;
    char buf[256];
    while (fgets(buf, (int)sizeof(buf), fp))
        out += buf;
    pclose(fp);
    return out;
}
#endif

/* Convert Windows signal percentage (0–100) to approximate dBm. */
static int pct_to_rssi(int pct) {
    /* 100% ≈ -30 dBm, 0% ≈ -100 dBm */
    return -100 + (pct * 70 / 100);
}

/* -------------------------------------------------------------------------
 * netsh output parser (used until WlanAPI is wired in)
 * ---------------------------------------------------------------------- */

struct WifiNet {
    std::string ssid;
    std::string bssid;
    int         rssi  = -127;
    std::string auth  = "wpa2";
    std::string band  = "2.4";
    int         chan  = 0;
};

/*
 * Parse `netsh wlan show networks mode=bssid` output.
 * Each SSID block starts with "SSID N : <name>" and may contain multiple
 * "BSSID N : <mac>" sub-blocks; we emit one WifiNet per BSSID.
 */
static std::vector<WifiNet> parse_netsh(const std::string &text) {
    std::vector<WifiNet> result;
    WifiNet cur;
    bool in_ssid   = false;
    bool in_bssid  = false;

    auto flush = [&]() {
        if (in_ssid && !cur.ssid.empty())
            result.push_back(cur);
    };

    const char *p = text.c_str();
    while (*p) {
        const char *nl = strchr(p, '\n');
        std::string raw(p, nl ? (size_t)(nl - p) : strlen(p));
        p = nl ? nl + 1 : p + raw.size();
        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back();
        std::string line = trim(raw);

        /* "SSID N : name" — must not contain "BSSID" */
        if (line.find("SSID ") == 0 && line.find("BSSID") == std::string::npos) {
            size_t colon = line.rfind(": ");
            if (colon != std::string::npos) {
                flush();
                cur   = WifiNet();
                in_ssid  = true;
                in_bssid = false;
                cur.ssid = trim(line.substr(colon + 2));
            }
            continue;
        }
        if (!in_ssid)
            continue;

        /* "Authentication : WPA2-Personal" */
        if (line.find("Authentication") != std::string::npos) {
            size_t c = line.find(": ");
            if (c != std::string::npos) {
                std::string v = trim(line.substr(c + 2));
                if (v.find("WPA3") != std::string::npos || v.find("SAE") != std::string::npos)
                    cur.auth = "wpa3";
                else if (v.find("WPA2") != std::string::npos || v.find("WPA") != std::string::npos)
                    cur.auth = "wpa2";
                else
                    cur.auth = "open";
            }
            continue;
        }

        /* "BSSID N : aa:bb:cc:dd:ee:ff" */
        if (line.find("BSSID ") == 0) {
            if (in_bssid) {
                /* Additional BSSID for same SSID — save current, clone for next */
                result.push_back(cur);
                std::string ssid = cur.ssid, auth = cur.auth;
                cur      = WifiNet();
                cur.ssid = ssid;
                cur.auth = auth;
            }
            in_bssid = true;
            size_t c = line.rfind(": ");
            if (c != std::string::npos)
                cur.bssid = trim(line.substr(c + 2));
            continue;
        }

        if (!in_bssid)
            continue;

        /* "Signal : 85%" */
        if (line.find("Signal") != std::string::npos && line.find('%') != std::string::npos) {
            size_t c = line.find(": ");
            if (c != std::string::npos)
                cur.rssi = pct_to_rssi(atoi(trim(line.substr(c + 2)).c_str()));
            continue;
        }

        /* "Radio type : 802.11ax" */
        if (line.find("Radio type") != std::string::npos) {
            size_t c = line.find(": ");
            if (c != std::string::npos) {
                std::string v = trim(line.substr(c + 2));
                /* 6 GHz uses 802.11ax and channel > 177; set tentatively */
                if (v.find("6GHz") != std::string::npos || v.find("6 GHz") != std::string::npos)
                    cur.band = "6";
            }
            continue;
        }

        /* "Channel : 6" */
        if (line.find("Channel") != std::string::npos) {
            size_t c = line.find(": ");
            if (c != std::string::npos) {
                cur.chan = atoi(trim(line.substr(c + 2)).c_str());
                if (cur.band != "6") {
                    if (cur.chan >= 36 && cur.chan <= 177)
                        cur.band = "5";
                    else if (cur.chan >= 1 && cur.chan <= 14)
                        cur.band = "2.4";
                }
            }
            continue;
        }
    }
    flush();
    return result;
}

/* -------------------------------------------------------------------------
 * Commands
 * ---------------------------------------------------------------------- */

static void cmd_wifi_scan(void) {
#if defined(_WIN32)
    /*
     * Current: delegate to netsh (always available on Windows).
     * TODO: replace with WlanEnumInterfaces + WlanScan + WlanGetNetworkBssList
     */
    std::string raw = capture("netsh wlan show networks mode=bssid 2>nul");
#else
    /* Linux/macOS development stub — mimics realistic netsh output. */
    std::string raw =
        "SSID 1 : FlintstoneTestNet\r\n"
        " Authentication          : WPA2-Personal\r\n"
        " BSSID 1                 : AA:BB:CC:DD:EE:FF\r\n"
        "      Signal             : 80%\r\n"
        "      Radio type         : 802.11n\r\n"
        "      Channel            : 6\r\n";
#endif

    std::vector<WifiNet> nets = parse_netsh(raw);
    for (const WifiNet &n : nets) {
        if (n.ssid.empty())
            continue;
        printf("ssid=%s\tbssid=%s\trssi=%d\tauth=%s\tband=%s\tchan=%d\n",
               n.ssid.c_str(),
               n.bssid.empty() ? "00:00:00:00:00:00" : n.bssid.c_str(),
               n.rssi,
               n.auth.c_str(),
               n.band.c_str(),
               n.chan);
    }
}

static void cmd_wifi_join(const char *ssid, const char *password) {
    (void)password; /* future: embed PSK in the profile XML */
#if defined(_WIN32)
    /*
     * Current: netsh connect requires a pre-existing Windows profile.
     * TODO: build a WPA2-PSK profile XML in memory and call WlanSetProfile +
     *       WlanConnect so arbitrary networks can be joined without a prior
     *       profile.
     */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "netsh wlan connect name=\"%s\" 2>nul", ssid);
    std::string out = capture(cmd);
    if (out.find("successfully") != std::string::npos ||
        out.find("completed") != std::string::npos) {
        printf("result=ok\tssid=%s\n", ssid);
    } else {
        fprintf(stderr,
                "FlinstonePowershell: wifi-join '%s': no saved Windows profile.\n"
                "  Create a profile in Windows first, or wait for WlanAPI integration.\n",
                ssid);
        printf("result=error\tmsg=No saved Windows profile for '%s'\n", ssid);
    }
#else
    /* Stub for Linux dev builds */
    printf("result=ok\tssid=%s\n", ssid);
#endif
}

static void cmd_wifi_leave(void) {
#if defined(_WIN32)
    /*
     * TODO: replace with WlanDisconnect
     */
    std::string out = capture("netsh wlan disconnect 2>nul");
    if (out.find("successfully") != std::string::npos ||
        out.find("completed") != std::string::npos)
        puts("result=ok");
    else
        puts("result=error\tmsg=Disconnect failed or not connected");
#else
    puts("result=ok");
#endif
}

static void cmd_wifi_status(void) {
#if defined(_WIN32)
    /*
     * TODO: replace with WlanQueryInterface(wlan_intf_opcode_current_connection)
     */
    std::string raw = capture("netsh wlan show interfaces 2>nul");
    std::string ssid;
    bool connected = false;

    const char *p = raw.c_str();
    while (*p) {
        const char *nl = strchr(p, '\n');
        std::string line(p, nl ? (size_t)(nl - p) : strlen(p));
        p = nl ? nl + 1 : p + line.size();
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        line = trim(line);

        if (line.find("State") != std::string::npos) {
            size_t c = line.find(": ");
            if (c != std::string::npos)
                connected = (trim(line.substr(c + 2)) == "connected");
        }
        /* "SSID" but not "BSSID" */
        if (line.find("SSID") != std::string::npos &&
            line.find("BSSID") == std::string::npos) {
            size_t c = line.find(": ");
            if (c != std::string::npos)
                ssid = trim(line.substr(c + 2));
        }
    }

    if (connected && !ssid.empty())
        printf("state=connected\tssid=%s\n", ssid.c_str());
    else if (connected)
        puts("state=connected");
    else
        puts("state=disconnected");
#else
    puts("state=disconnected");
#endif
}

static void cmd_platform(void) {
#if defined(_WIN32)
    puts("platform=windows");
#elif defined(__linux__)
    puts("platform=linux");
#elif defined(__APPLE__)
    puts("platform=macos");
#else
    puts("platform=unknown");
#endif
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "FlinstonePowershell — Windows Wi-Fi bridge for Bailey-Forbes-Flinstone\n"
                "\n"
                "Usage: FlinstonePowershell <command> [args...]\n"
                "\n"
                "Commands:\n"
                "  platform                    Print the host platform tag\n"
                "  wifi-scan                   List visible Wi-Fi networks\n"
                "  wifi-join <ssid> [password] Connect to a network\n"
                "  wifi-leave                  Disconnect from current network\n"
                "  wifi-status                 Show connection state\n");
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "platform") == 0) {
        cmd_platform();
    } else if (strcmp(cmd, "wifi-scan") == 0) {
        cmd_wifi_scan();
    } else if (strcmp(cmd, "wifi-join") == 0) {
        if (argc < 3) {
            fprintf(stderr, "wifi-join: requires <ssid>\n");
            return 1;
        }
        cmd_wifi_join(argv[2], argc >= 4 ? argv[3] : "");
    } else if (strcmp(cmd, "wifi-leave") == 0) {
        cmd_wifi_leave();
    } else if (strcmp(cmd, "wifi-status") == 0) {
        cmd_wifi_status();
    } else {
        fprintf(stderr, "FlinstonePowershell: unknown command '%s'\n", cmd);
        return 1;
    }
    return 0;
}
