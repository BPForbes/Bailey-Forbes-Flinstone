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
 * Windows / mingw32 cross-compile (full WlanAPI support):
 *   x86_64-w64-mingw32-g++ -std=c++17 -o FlinstonePowershell.exe \
 *       FlinstonePowershell.cpp -lwlanapi -lole32
 *
 * Linux / WSL (development build, uses dev stubs):
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
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0600  /* Vista: enables GAA_FLAG_INCLUDE_GATEWAYS, IP_ADAPTER_GATEWAY_ADDRESS_LH, etc. */
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wlanapi.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "iphlpapi.lib")
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

/* -------------------------------------------------------------------------
 * Windows-only helpers
 * ---------------------------------------------------------------------- */

#if defined(_WIN32)

/* RAII handle for WlanAPI client handle. */
struct WlanHandle {
    HANDLE  h   = nullptr;
    DWORD   ver = 0;

    WlanHandle() {
        DWORD cur = 0;
        if (WlanOpenHandle(2, nullptr, &cur, &h) != ERROR_SUCCESS) {
            h   = nullptr;
            ver = 0;
        } else {
            ver = cur;
        }
    }

    ~WlanHandle() {
        if (h) {
            WlanCloseHandle(h, nullptr);
            h = nullptr;
        }
    }

    bool ok() const { return h != nullptr; }
};

/* Convert a UTF-8 std::string to a Windows WCHAR string. */
static std::wstring to_wide(const std::string &s) {
    if (s.empty())
        return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0)
        return L"";
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

/* Convert a WLAN DOT11_SSID to a UTF-8 std::string. */
static std::string ssid_to_str(const DOT11_SSID &s) {
    return std::string(reinterpret_cast<const char *>(s.ucSSID), s.uSSIDLength);
}

/* Convert a DOT11_MAC_ADDRESS to "aa:bb:cc:dd:ee:ff". */
static std::string mac_to_str(const DOT11_MAC_ADDRESS &m) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    return buf;
}

/* Escape a string for inclusion inside XML element text or attribute values. */
static std::string xml_escape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default:   out += (char)c;  break;
        }
    }
    return out;
}

/*
 * Derive auth mode from the raw 802.11 Information Elements blob.
 *   RSN IE (tag 0x30): WPA2 or WPA3 (AKM suite 00:0F:AC:08 = SAE → WPA3)
 *   Vendor IE (0xDD, OUI 00:50:F2, type 0x01): WPA1
 * Returns "open", "wpa2", or "wpa3".
 */
static std::string auth_from_ies(const UCHAR *ies, ULONG ie_len) {
    bool has_wpa  = false;
    bool has_rsn  = false;
    bool has_sae  = false;

    const UCHAR *p   = ies;
    const UCHAR *end = ies + ie_len;

    while (p + 2 <= end) {
        UCHAR tag = p[0];
        UCHAR len = p[1];
        const UCHAR *body = p + 2;

        if (body + len > end)
            break;

        if (tag == 0x30 && len >= 2) {
            /* RSN IE: skip version (2) + group cipher (4) */
            has_rsn = true;
            if (len >= 8) {
                UINT16 pairwise_cnt = (UINT16)(body[4] | ((UINT16)body[5] << 8));
                const UCHAR *akm_start = body + 6 + pairwise_cnt * 4;
                if (akm_start + 2 <= body + len) {
                    UINT16 akm_cnt = (UINT16)(akm_start[0] | ((UINT16)akm_start[1] << 8));
                    const UCHAR *akm = akm_start + 2;
                    for (UINT16 i = 0; i < akm_cnt && akm + 4 <= body + len; i++, akm += 4) {
                        /* OUI 00:0F:AC, type 08 = SAE (WPA3-Personal) */
                        if (akm[0] == 0x00 && akm[1] == 0x0F &&
                            akm[2] == 0xAC && akm[3] == 0x08) {
                            has_sae = true;
                        }
                    }
                }
            }
        } else if (tag == 0xDD && len >= 4) {
            /* Vendor IE: OUI 00:50:F2, type 01 = Microsoft WPA */
            if (body[0] == 0x00 && body[1] == 0x50 &&
                body[2] == 0xF2 && body[3] == 0x01) {
                has_wpa = true;
            }
        }

        p += 2 + len;
    }

    if (has_sae)
        return "wpa3";
    if (has_rsn)
        return "wpa2";
    if (has_wpa)
        return "wpa2"; /* WPA1 — treat as wpa2 for display */
    return "open";
}

/* Convert centre frequency in kHz to 802.11 channel number. */
static int chan_from_khz(ULONG freq_khz) {
    ULONG mhz = freq_khz / 1000;

    /* 2.4 GHz band: 2412 MHz = ch1, 5 MHz steps */
    if (mhz >= 2412 && mhz <= 2484) {
        if (mhz == 2484)
            return 14;
        return (int)((mhz - 2407) / 5);
    }

    /* 5 GHz band: 5180 MHz = ch36, 5 MHz steps */
    if (mhz >= 5160 && mhz <= 5885)
        return (int)((mhz - 5000) / 5);

    /* 6 GHz band: 5955 MHz = ch1, 5 MHz steps */
    if (mhz >= 5935 && mhz <= 7115)
        return (int)((mhz - 5950) / 5 + 1);

    return 0;
}

/* Convert centre frequency in kHz to band string "2.4", "5", or "6". */
static const char *band_from_khz(ULONG freq_khz) {
    ULONG mhz = freq_khz / 1000;
    if (mhz >= 2400 && mhz < 2500)
        return "2.4";
    if (mhz >= 5000 && mhz < 5900)
        return "5";
    if (mhz >= 5900 && mhz <= 7200)
        return "6";
    return "2.4";
}

/*
 * Build a WLANProfile XML document for WlanSetProfile.
 * Produces a WPA2-PSK/AES profile when password is non-empty; otherwise open.
 */
static std::string build_profile_xml(const std::string &ssid,
                                     const std::string &password) {
    bool open = password.empty();
    std::string xs = xml_escape(ssid);
    std::string xp = xml_escape(password);

    std::string xml;
    xml += "<?xml version=\"1.0\"?>\r\n";
    xml += "<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\r\n";
    xml += "\t<name>" + xs + "</name>\r\n";
    xml += "\t<SSIDConfig>\r\n";
    xml += "\t\t<SSID>\r\n";
    xml += "\t\t\t<name>" + xs + "</name>\r\n";
    xml += "\t\t</SSID>\r\n";
    xml += "\t</SSIDConfig>\r\n";
    xml += "\t<connectionType>ESS</connectionType>\r\n";
    xml += "\t<connectionMode>manual</connectionMode>\r\n";
    xml += "\t<MSM>\r\n";
    xml += "\t\t<security>\r\n";
    if (open) {
        xml += "\t\t\t<authEncryption>\r\n";
        xml += "\t\t\t\t<authentication>open</authentication>\r\n";
        xml += "\t\t\t\t<encryption>none</encryption>\r\n";
        xml += "\t\t\t\t<useOneX>false</useOneX>\r\n";
        xml += "\t\t\t</authEncryption>\r\n";
    } else {
        xml += "\t\t\t<authEncryption>\r\n";
        xml += "\t\t\t\t<authentication>WPA2PSK</authentication>\r\n";
        xml += "\t\t\t\t<encryption>AES</encryption>\r\n";
        xml += "\t\t\t\t<useOneX>false</useOneX>\r\n";
        xml += "\t\t\t</authEncryption>\r\n";
        xml += "\t\t\t<sharedKey>\r\n";
        xml += "\t\t\t\t<keyType>passPhrase</keyType>\r\n";
        xml += "\t\t\t\t<protected>false</protected>\r\n";
        xml += "\t\t\t\t<keyMaterial>" + xp + "</keyMaterial>\r\n";
        xml += "\t\t\t</sharedKey>\r\n";
    }
    xml += "\t\t</security>\r\n";
    xml += "\t</MSM>\r\n";
    xml += "</WLANProfile>\r\n";
    return xml;
}

/*
 * Look up the IPv4 address, prefix length, and default gateway for the
 * Windows adapter identified by a GUID.  Fills ipv4 (dotted-decimal),
 * *prefix (prefix length in bits), and gw (gateway dotted-decimal).
 * Returns true on success, false if the adapter was not found or has no
 * suitable address.
 */
static bool get_iface_addr(const GUID *guid, char *ipv4, size_t ip_len,
                           UINT8 *prefix, char *gw, size_t gw_len) {
    /* Build lowercase dashed GUID string without braces. */
    char guid_str[37];
    snprintf(guid_str, sizeof(guid_str),
             "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             (unsigned)guid->Data1,
             (unsigned)guid->Data2,
             (unsigned)guid->Data3,
             (unsigned)guid->Data4[0], (unsigned)guid->Data4[1],
             (unsigned)guid->Data4[2], (unsigned)guid->Data4[3],
             (unsigned)guid->Data4[4], (unsigned)guid->Data4[5],
             (unsigned)guid->Data4[6], (unsigned)guid->Data4[7]);

    /* Query buffer size then allocate. */
    ULONG size = 0;
    GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, NULL, &size);
    if (size == 0)
        return false;

    std::vector<BYTE> buf(size);
    IP_ADAPTER_ADDRESSES *list =
        reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, list,
                             &size) != ERROR_SUCCESS)
        return false;

    for (IP_ADAPTER_ADDRESSES *a = list; a; a = a->Next) {
        /* AdapterName is the GUID string without braces. */
        if (_stricmp(a->AdapterName, guid_str) != 0)
            continue;

        /* First unicast IPv4 address. */
        IP_ADAPTER_UNICAST_ADDRESS *ua = a->FirstUnicastAddress;
        if (!ua || !ua->Address.lpSockaddr)
            return false;
        struct sockaddr_in *sin =
            reinterpret_cast<struct sockaddr_in *>(ua->Address.lpSockaddr);
        if (!inet_ntop(AF_INET, &sin->sin_addr, ipv4, (socklen_t)ip_len))
            return false;
        if (prefix)
            *prefix = ua->OnLinkPrefixLength;

        /* First gateway. */
        if (gw && gw_len > 0) {
            gw[0] = '\0';
            IP_ADAPTER_GATEWAY_ADDRESS_LH *gwaddr = a->FirstGatewayAddress;
            if (gwaddr && gwaddr->Address.lpSockaddr) {
                struct sockaddr_in *gsin =
                    reinterpret_cast<struct sockaddr_in *>(
                        gwaddr->Address.lpSockaddr);
                inet_ntop(AF_INET, &gsin->sin_addr, gw, (socklen_t)gw_len);
            }
        }
        return true;
    }
    return false;
}

#endif /* _WIN32 */

/* -------------------------------------------------------------------------
 * Commands
 * ---------------------------------------------------------------------- */

static void cmd_wifi_scan(void) {
#if defined(_WIN32)
    WlanHandle wlan;
    if (!wlan.ok()) {
        fprintf(stderr, "FlinstonePowershell: WlanOpenHandle failed\n");
        return;
    }

    PWLAN_INTERFACE_INFO_LIST iface_list = nullptr;
    if (WlanEnumInterfaces(wlan.h, nullptr, &iface_list) != ERROR_SUCCESS ||
        !iface_list || iface_list->dwNumberOfItems == 0) {
        if (iface_list)
            WlanFreeMemory(iface_list);
        fprintf(stderr, "FlinstonePowershell: no wireless interfaces\n");
        return;
    }

    for (DWORD i = 0; i < iface_list->dwNumberOfItems; i++) {
        GUID *guid = &iface_list->InterfaceInfo[i].InterfaceGuid;

        /* Trigger a scan and wait for the radio to settle. */
        WlanScan(wlan.h, guid, nullptr, nullptr, nullptr);
        Sleep(1500);

        PWLAN_BSS_LIST bss_list = nullptr;
        if (WlanGetNetworkBssList(wlan.h, guid, nullptr,
                                  dot11_BSS_type_infrastructure,
                                  FALSE, nullptr, &bss_list) != ERROR_SUCCESS ||
            !bss_list) {
            if (bss_list)
                WlanFreeMemory(bss_list);
            continue;
        }

        for (ULONG j = 0; j < bss_list->dwNumberOfItems; j++) {
            const WLAN_BSS_ENTRY &e = bss_list->wlanBssEntries[j];

            std::string ssid = ssid_to_str(e.dot11Ssid);
            if (ssid.empty())
                continue;

            std::string bssid = mac_to_str(e.dot11Bssid);
            int         rssi  = (int)e.lRssi;

            const UCHAR *ies    = reinterpret_cast<const UCHAR *>(&e) +
                                  e.ulIeOffset;
            ULONG        ie_len = e.ulIeSize;
            std::string  auth   = auth_from_ies(ies, ie_len);
            const char  *band   = band_from_khz(e.ulChCenterFrequency);
            int          chan   = chan_from_khz(e.ulChCenterFrequency);

            printf("ssid=%s\tbssid=%s\trssi=%d\tauth=%s\tband=%s\tchan=%d\n",
                   ssid.c_str(), bssid.c_str(), rssi,
                   auth.c_str(), band, chan);
        }
        WlanFreeMemory(bss_list);
    }
    WlanFreeMemory(iface_list);

#else
    /* Linux/macOS development stub — mimics realistic scan output. */
    printf("ssid=FlintstoneTestNet\tbssid=AA:BB:CC:DD:EE:FF\trssi=-56\tauth=wpa2\tband=2.4\tchan=6\n");
#endif
}

static void cmd_wifi_join(const char *ssid, const char *password) {
#if defined(_WIN32)
    WlanHandle wlan;
    if (!wlan.ok()) {
        printf("result=error\tmsg=WlanOpenHandle failed\n");
        return;
    }

    PWLAN_INTERFACE_INFO_LIST iface_list = nullptr;
    if (WlanEnumInterfaces(wlan.h, nullptr, &iface_list) != ERROR_SUCCESS ||
        !iface_list || iface_list->dwNumberOfItems == 0) {
        if (iface_list)
            WlanFreeMemory(iface_list);
        printf("result=error\tmsg=No wireless interfaces\n");
        return;
    }

    /* Build and install the profile on the first interface. */
    GUID *guid = &iface_list->InterfaceInfo[0].InterfaceGuid;
    std::string xml = build_profile_xml(ssid, password ? password : "");
    std::wstring wxml = to_wide(xml);

    DWORD reason = 0;
    DWORD rc = WlanSetProfile(wlan.h, guid, 0, wxml.c_str(),
                              nullptr, TRUE, nullptr, &reason);
    if (rc != ERROR_SUCCESS) {
        WlanFreeMemory(iface_list);
        char msg[128];
        snprintf(msg, sizeof(msg), "WlanSetProfile failed (rc=%lu reason=%lu)", rc, reason);
        printf("result=error\tmsg=%s\n", msg);
        return;
    }

    /* Connect using the newly installed profile. */
    std::wstring wssid = to_wide(ssid);
    WLAN_CONNECTION_PARAMETERS params = {};
    params.wlanConnectionMode = wlan_connection_mode_profile;
    params.strProfile         = wssid.c_str();
    params.pDot11Ssid         = nullptr;
    params.pDesiredBssidList  = nullptr;
    params.dot11BssType       = dot11_BSS_type_infrastructure;
    params.dwFlags            = 0;

    rc = WlanConnect(wlan.h, guid, &params, nullptr);
    WlanFreeMemory(iface_list);

    if (rc == ERROR_SUCCESS) {
        printf("result=ok\tssid=%s\n", ssid);
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "WlanConnect failed (rc=%lu)", rc);
        printf("result=error\tmsg=%s\n", msg);
    }
#else
    /* Stub for Linux dev builds */
    (void)password;
    printf("result=ok\tssid=%s\n", ssid);
#endif
}

static void cmd_wifi_leave(void) {
#if defined(_WIN32)
    WlanHandle wlan;
    if (!wlan.ok()) {
        puts("result=error\tmsg=WlanOpenHandle failed");
        return;
    }

    PWLAN_INTERFACE_INFO_LIST iface_list = nullptr;
    if (WlanEnumInterfaces(wlan.h, nullptr, &iface_list) != ERROR_SUCCESS ||
        !iface_list || iface_list->dwNumberOfItems == 0) {
        if (iface_list)
            WlanFreeMemory(iface_list);
        puts("result=error\tmsg=No wireless interfaces");
        return;
    }

    DWORD rc = ERROR_SUCCESS;
    for (DWORD i = 0; i < iface_list->dwNumberOfItems; i++) {
        DWORD r = WlanDisconnect(wlan.h,
                                 &iface_list->InterfaceInfo[i].InterfaceGuid,
                                 nullptr);
        if (r != ERROR_SUCCESS)
            rc = r;
    }
    WlanFreeMemory(iface_list);

    if (rc == ERROR_SUCCESS)
        puts("result=ok");
    else
        puts("result=error\tmsg=Disconnect failed or not connected");
#else
    puts("result=ok");
#endif
}

static void cmd_wifi_status(void) {
#if defined(_WIN32)
    WlanHandle wlan;
    if (!wlan.ok()) {
        puts("state=disconnected");
        return;
    }

    PWLAN_INTERFACE_INFO_LIST iface_list = nullptr;
    if (WlanEnumInterfaces(wlan.h, nullptr, &iface_list) != ERROR_SUCCESS ||
        !iface_list || iface_list->dwNumberOfItems == 0) {
        if (iface_list)
            WlanFreeMemory(iface_list);
        puts("state=disconnected");
        return;
    }

    bool printed = false;
    for (DWORD i = 0; i < iface_list->dwNumberOfItems; i++) {
        GUID *guid = &iface_list->InterfaceInfo[i].InterfaceGuid;

        PWLAN_CONNECTION_ATTRIBUTES attrs = nullptr;
        DWORD size = sizeof(WLAN_CONNECTION_ATTRIBUTES);
        if (WlanQueryInterface(wlan.h, guid,
                               wlan_intf_opcode_current_connection,
                               nullptr, &size,
                               reinterpret_cast<PVOID *>(&attrs),
                               nullptr) == ERROR_SUCCESS && attrs) {
            if (attrs->isState == wlan_interface_state_connected) {
                std::string ssid = ssid_to_str(
                    attrs->wlanAssociationAttributes.dot11Ssid);

                /* Query the real Windows adapter IP for this interface. */
                char ipv4[16] = "";
                char gw[16]   = "";
                UINT8 pfx     = 24;
                bool have_ip  = get_iface_addr(guid, ipv4, sizeof(ipv4),
                                               &pfx, gw, sizeof(gw));

                if (!ssid.empty()) {
                    if (have_ip && ipv4[0])
                        printf("state=connected\tssid=%s\tipv4=%s\tprefix=%u\tgateway=%s\n",
                               ssid.c_str(), ipv4, (unsigned)pfx, gw);
                    else
                        printf("state=connected\tssid=%s\n", ssid.c_str());
                } else {
                    if (have_ip && ipv4[0])
                        printf("state=connected\tipv4=%s\tprefix=%u\tgateway=%s\n",
                               ipv4, (unsigned)pfx, gw);
                    else
                        puts("state=connected");
                }
                printed = true;
            }
            WlanFreeMemory(attrs);
        }
        if (printed)
            break;
    }
    WlanFreeMemory(iface_list);

    if (!printed)
        puts("state=disconnected");
#else
    /* Linux dev stub — real IP fields are only available in the Windows build. */
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
 * server-proxy / server-proxy-del
 * Use Windows netsh portproxy to forward <win-ip>:<port> → <wsl-ip>:<port>
 * so LAN peers can reach a server running inside WSL.
 * Requires the process to run with Windows administrator rights.
 * ---------------------------------------------------------------------- */

static void cmd_server_proxy(const char *wsl_ip, const char *port_s) {
#if defined(_WIN32)
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "netsh interface portproxy add v4tov4"
             " listenaddress=0.0.0.0 listenport=%s"
             " connectaddress=%s connectport=%s",
             port_s, wsl_ip, port_s);
    int rc = system(cmd);
    if (rc == 0)
        printf("result=ok\tport=%s\twsl_ip=%s\n", port_s, wsl_ip);
    else
        printf("result=err\tmsg=netsh portproxy failed (rc=%d, run as admin)\n", rc);
#else
    (void)wsl_ip; (void)port_s;
    puts("result=err\tmsg=server-proxy only available on Windows");
#endif
}

static void cmd_server_proxy_del(const char *port_s) {
#if defined(_WIN32)
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "netsh interface portproxy delete v4tov4"
             " listenaddress=0.0.0.0 listenport=%s",
             port_s);
    int rc = system(cmd);
    if (rc == 0)
        printf("result=ok\tport=%s\n", port_s);
    else
        printf("result=err\tmsg=netsh portproxy delete failed (rc=%d)\n", rc);
#else
    (void)port_s;
    puts("result=err\tmsg=server-proxy-del only available on Windows");
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
                "  platform                          Print the host platform tag\n"
                "  wifi-scan                         List visible Wi-Fi networks\n"
                "  wifi-join <ssid> [password]       Connect to a network\n"
                "  wifi-leave                        Disconnect from current network\n"
                "  wifi-status                       Show connection state\n"
                "  server-proxy <wsl_ip> <port>      Forward Windows IP:<port> → WSL (admin)\n"
                "  server-proxy-del <port>           Remove portproxy rule (admin)\n");
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
    } else if (strcmp(cmd, "server-proxy") == 0) {
        if (argc < 4) {
            fprintf(stderr, "server-proxy: requires <wsl_ip> <port>\n");
            return 1;
        }
        cmd_server_proxy(argv[2], argv[3]);
    } else if (strcmp(cmd, "server-proxy-del") == 0) {
        if (argc < 3) {
            fprintf(stderr, "server-proxy-del: requires <port>\n");
            return 1;
        }
        cmd_server_proxy_del(argv[2]);
    } else {
        fprintf(stderr, "FlinstonePowershell: unknown command '%s'\n", cmd);
        return 1;
    }
    return 0;
}
