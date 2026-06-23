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
 *       FlinstonePowershell.cpp -lwlanapi -lole32 -liphlpapi
 *
 * Linux / WSL (development build, uses dev stubs):
 *   g++ -std=c++17 -o FlinstonePowershell FlinstonePowershell.cpp
 *
 * stdout protocol (consumed by parse_flinstone_ps_scan in net_wifi_host_linux.c)
 * -------------------------------------------------------------------------------
 * wifi-scan  : one line per network, fields tab-separated:
 *   ssid=<name>\tbssid=<aa:bb:cc:dd:ee:ff>\trssi=<dBm>\tauth=<open|wpa2|wpa3>\tband=<2.4|5|6>\tchan=<n>\the=<0|1>[\tcolor=<0-63>]
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
#  define _WIN32_WINNT 0x0600  /* Vista: GAA_FLAG_INCLUDE_GATEWAYS, gateway fields */
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wlanapi.h>
#include <iphlpapi.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

/* -------------------------------------------------------------------------
 * Input validation helpers
 * ---------------------------------------------------------------------- */

static bool is_valid_ipv4(const char *s) {
    if (!s || !*s)
        return false;
    size_t len = strlen(s);
    if (len > 15)
        return false;
    for (size_t i = 0; i < len; i++) {
        if (s[i] != '.' && (s[i] < '0' || s[i] > '9'))
            return false;
    }
    return true;
}

static bool is_valid_port(const char *s) {
    if (!s || !*s)
        return false;
    for (size_t i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9')
            return false;
    }
    int p = atoi(s);
    return p > 0 && p <= 65535;
}

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
            /* RSN IE: version(2) + group cipher(4) + pairwise count(2) + suites */
            has_rsn = true;
            if (len >= 8) {
                /* pairwise count is at body[6..7], after version(2) + group cipher(4) */
                UINT16 pairwise_cnt = (UINT16)(body[6] | ((UINT16)body[7] << 8));
                const UCHAR *akm_start = body + 8 + pairwise_cnt * 4;
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

/* Extension IE 255 / HE Capabilities (ext 35) → 802.11ax. */
static int he_from_ies(const UCHAR *ies, ULONG ie_len) {
    const UCHAR *p   = ies;
    const UCHAR *end = ies + ie_len;

    while (p + 2 <= end) {
        UCHAR tag = p[0];
        UCHAR len = p[1];
        const UCHAR *body = p + 2;

        if (body + len > end)
            break;
        if (tag == 0xFF && len >= 1 && body[0] == 0x23) /* HE Capabilities */
            return 1;
        p += 2 + len;
    }
    return 0;
}

/* HE Operation (ext 36): BSS Color in byte 4 bits 0-5 after ext id. */
static int he_bss_color_from_ies(const UCHAR *ies, ULONG ie_len) {
    const UCHAR *p   = ies;
    const UCHAR *end = ies + ie_len;

    while (p + 2 <= end) {
        UCHAR tag = p[0];
        UCHAR len = p[1];
        const UCHAR *body = p + 2;

        if (body + len > end)
            break;
        if (tag == 0xFF && len >= 5 && body[0] == 0x24) {
            int color = (int)(body[4] & 0x3Fu);
            return (color >= 0 && color <= 63) ? color : -1;
        }
        p += 2 + len;
    }
    return -1;
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

    /* 6 GHz band: 5955 MHz = ch1, 5 MHz steps; lower bound >=5955 avoids
     * unsigned underflow (ULONG): (5935-5950) wraps on unsigned arithmetic. */
    if (mhz >= 5955 && mhz <= 7115)
        return (int)((mhz - 5950) / 5);

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
 * Copy the first unicast IPv4 (+ gateway) from an IP_ADAPTER_ADDRESSES row.
 */
static bool fill_ipv4_from_adapter(IP_ADAPTER_ADDRESSES *a, char *ipv4, size_t ip_len,
                                 UINT8 *prefix, char *gw, size_t gw_len) {
    IP_ADAPTER_UNICAST_ADDRESS *ua;

    if (!a)
        return false;
    for (ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
        struct sockaddr_in *sin;

        if (!ua->Address.lpSockaddr ||
            ua->Address.lpSockaddr->sa_family != AF_INET)
            continue;
        sin = reinterpret_cast<struct sockaddr_in *>(ua->Address.lpSockaddr);
        if (!inet_ntop(AF_INET, &sin->sin_addr, ipv4, (socklen_t)ip_len))
            continue;
        if (prefix)
            *prefix = ua->OnLinkPrefixLength;
        if (gw && gw_len > 0) {
            IP_ADAPTER_GATEWAY_ADDRESS_LH *gwaddr = a->FirstGatewayAddress;
            gw[0] = '\0';
            if (gwaddr && gwaddr->Address.lpSockaddr &&
                gwaddr->Address.lpSockaddr->sa_family == AF_INET) {
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
    char guid_braced[39];
    ULONG size = 0;
    IP_ADAPTER_ADDRESSES *list;
    IP_ADAPTER_ADDRESSES *a;
    IP_ADAPTER_ADDRESSES *wifi_up = nullptr;

    snprintf(guid_str, sizeof(guid_str),
             "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             (unsigned)guid->Data1,
             (unsigned)guid->Data2,
             (unsigned)guid->Data3,
             (unsigned)guid->Data4[0], (unsigned)guid->Data4[1],
             (unsigned)guid->Data4[2], (unsigned)guid->Data4[3],
             (unsigned)guid->Data4[4], (unsigned)guid->Data4[5],
             (unsigned)guid->Data4[6], (unsigned)guid->Data4[7]);
    snprintf(guid_braced, sizeof(guid_braced), "{%s}", guid_str);

    GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, NULL, &size);
    if (size == 0)
        return false;

    std::vector<BYTE> buf(size);
    list = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, list,
                             &size) != ERROR_SUCCESS)
        return false;

    for (a = list; a; a = a->Next) {
        bool guid_match = (_stricmp(a->AdapterName, guid_str) == 0 ||
                           _stricmp(a->AdapterName, guid_braced) == 0);
        if (guid_match)
            return fill_ipv4_from_adapter(a, ipv4, ip_len, prefix, gw, gw_len);
        /* IF_TYPE_IEEE80211 == 71 — fallback when GUID string form differs. */
        if (a->IfType == 71u && a->OperStatus == IfOperStatusUp)
            wifi_up = a;
    }
    if (wifi_up)
        return fill_ipv4_from_adapter(wifi_up, ipv4, ip_len, prefix, gw, gw_len);
    return false;
}

/* Return true when a saved Windows profile's XML contains this SSID. */
static bool profile_xml_has_ssid(const WCHAR *xml, const std::wstring &ssid) {
    const WCHAR *needle;
    if (!xml || ssid.empty())
        return false;
    needle = wcsstr(xml, ssid.c_str());
    return needle != nullptr;
}

/* Connect using an already-saved profile whose XML references the SSID. */
static bool try_connect_saved_profile(HANDLE wlan, GUID *guid,
                                      const std::string &ssid) {
    PWLAN_PROFILE_INFO_LIST profiles = nullptr;
    std::wstring wssid = to_wide(ssid);
    bool connected = false;

    if (WlanGetProfileList(wlan, guid, nullptr, &profiles) != ERROR_SUCCESS ||
        !profiles)
        return false;

    for (DWORD i = 0; i < profiles->dwNumberOfItems; i++) {
        WCHAR *xml = nullptr;
        DWORD flags = 0;
        DWORD reason = 0;
        const WCHAR *pname = profiles->ProfileInfo[i].strProfileName;
        WLAN_CONNECTION_PARAMETERS params = {};

        if (WlanGetProfile(wlan, guid, pname, nullptr, &xml, &flags,
                           &reason) != ERROR_SUCCESS || !xml)
            continue;
        if (!profile_xml_has_ssid(xml, wssid)) {
            WlanFreeMemory(xml);
            continue;
        }
        WlanFreeMemory(xml);

        params.wlanConnectionMode = wlan_connection_mode_profile;
        params.strProfile         = pname;
        params.dot11BssType       = dot11_BSS_type_infrastructure;
        if (WlanConnect(wlan, guid, &params, nullptr) == ERROR_SUCCESS) {
            connected = true;
            break;
        }
    }
    WlanFreeMemory(profiles);
    return connected;
}

/* Connect with in-memory XML — never persisted (no "SSID 2" duplicates). */
static bool connect_temporary_profile(HANDLE wlan, GUID *guid,
                                      const std::string &ssid,
                                      const char *password) {
    std::string xml = build_profile_xml(ssid, password ? password : "");
    std::wstring wxml = to_wide(xml);
    WLAN_CONNECTION_PARAMETERS params = {};

    params.wlanConnectionMode = wlan_connection_mode_temporary_profile;
    params.strProfile         = wxml.c_str();
    params.dot11BssType       = dot11_BSS_type_infrastructure;
    return WlanConnect(wlan, guid, &params, nullptr) == ERROR_SUCCESS;
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
            int          he     = he_from_ies(ies, ie_len);
            int          color  = he_bss_color_from_ies(ies, ie_len);

            if (color >= 0)
                printf("ssid=%s\tbssid=%s\trssi=%d\tauth=%s\tband=%s\tchan=%d\the=%d\tcolor=%d\n",
                       ssid.c_str(), bssid.c_str(), rssi,
                       auth.c_str(), band, chan, he, color);
            else
                printf("ssid=%s\tbssid=%s\trssi=%d\tauth=%s\tband=%s\tchan=%d\the=%d\n",
                       ssid.c_str(), bssid.c_str(), rssi,
                       auth.c_str(), band, chan, he);
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

    GUID *guid = &iface_list->InterfaceInfo[0].InterfaceGuid;

    /* Already on this SSID — do not touch saved profiles. */
    {
        PWLAN_CONNECTION_ATTRIBUTES attrs = nullptr;
        DWORD attr_size = sizeof(WLAN_CONNECTION_ATTRIBUTES);
        if (WlanQueryInterface(wlan.h, guid, wlan_intf_opcode_current_connection,
                               nullptr, &attr_size,
                               reinterpret_cast<PVOID *>(&attrs),
                               nullptr) == ERROR_SUCCESS && attrs) {
            if (attrs->isState == wlan_interface_state_connected) {
                std::string cur = ssid_to_str(
                    attrs->wlanAssociationAttributes.dot11Ssid);
                WlanFreeMemory(attrs);
                if (cur == ssid) {
                    printf("result=ok\tssid=%s\n", ssid);
                    WlanFreeMemory(iface_list);
                    return;
                }
            } else {
                WlanFreeMemory(attrs);
            }
        }
    }

    /* Passwordless re-join: use any saved profile that references this SSID
     * (profile name may be "ForbesHoltgrave 2" while SSID is ForbesHoltgrave). */
    if (!password || !password[0]) {
        if (try_connect_saved_profile(wlan.h, guid, ssid)) {
            printf("result=ok\tssid=%s\n", ssid);
            WlanFreeMemory(iface_list);
            return;
        }
    }

    /* Credential join: temporary in-memory profile only — never WlanSetProfile
     * (persisted profiles are what create duplicate "SSID 2" entries). */
    if (connect_temporary_profile(wlan.h, guid, ssid, password)) {
        printf("result=ok\tssid=%s\n", ssid);
    } else {
        puts("result=error\tmsg=WlanConnect temporary profile failed");
    }
    WlanFreeMemory(iface_list);
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
 * server-bridge
 * Userspace TCP relay: listens on 0.0.0.0:<port> (all Windows adapters,
 * including Wi-Fi) or a specific router-assigned IP and forwards each accepted
 * connection to <target_ip>:<port> (default 127.0.0.1, WSL2 loopback).
 * No administrator rights required for ports >= 1024.
 * Runs in the foreground until killed; spawn with & from WSL.
 * ---------------------------------------------------------------------- */

#if defined(_WIN32)
struct relay_pair {
    SOCKET a, b;  /* a = LAN client, b = WSL loopback */
    LONG   refs;  /* starts 2; last thread to exit closes both sockets */
};

struct relay_task {
    SOCKET      src;
    SOCKET      dst;
    relay_pair *pair;
};

static DWORD WINAPI relay_thread(LPVOID arg) {
    relay_task *task = reinterpret_cast<relay_task *>(arg);
    SOCKET      src  = task->src;
    SOCKET      dst  = task->dst;
    relay_pair *pair = task->pair;
    free(task);

    char buf[4096];
    int  n;
    while ((n = recv(src, buf, (int)sizeof(buf), 0)) > 0) {
        int off = 0;
        while (off < n) {
            int s = send(dst, buf + off, n - off, 0);
            if (s == SOCKET_ERROR)
                goto done;
            off += s;
        }
    }
done:
    shutdown(dst, SD_SEND);
    if (InterlockedDecrement(&pair->refs) == 0) {
        closesocket(pair->a);
        closesocket(pair->b);
        free(pair);
    }
    return 0;
}
#endif /* _WIN32 */

static void cmd_server_bridge(const char *bind_ip, const char *port_s,
                              const char *target_ip) {
#if defined(_WIN32)
    int port = atoi(port_s);
    const char *target = (target_ip && target_ip[0]) ? target_ip : "127.0.0.1";
    if (port <= 0 || port > 65535) {
        printf("result=err\tmsg=invalid port\n");
        return;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("result=err\tmsg=WSAStartup failed (%d)\n", WSAGetLastError());
        return;
    }

    SOCKET lsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (lsock == INVALID_SOCKET) {
        printf("result=err\tmsg=socket failed (%d)\n", WSAGetLastError());
        WSACleanup();
        return;
    }
    int yes = 1;
    setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    /* Bind to specific IP when given; INADDR_ANY (0.0.0.0) otherwise. */
    if (bind_ip && bind_ip[0] && strcmp(bind_ip, "0.0.0.0") != 0) {
        addr.sin_addr.s_addr = inet_addr(bind_ip);
        if (addr.sin_addr.s_addr == INADDR_NONE) {
            printf("result=err\tmsg=invalid bind address '%s'\n", bind_ip);
            closesocket(lsock);
            WSACleanup();
            return;
        }
    } else {
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    addr.sin_port = htons((USHORT)port);

    if (bind(lsock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("result=err\tmsg=bind failed (%d)\n", WSAGetLastError());
        closesocket(lsock);
        WSACleanup();
        return;
    }
    if (listen(lsock, SOMAXCONN) == SOCKET_ERROR) {
        printf("result=err\tmsg=listen failed (%d)\n", WSAGetLastError());
        closesocket(lsock);
        WSACleanup();
        return;
    }

    printf("result=ok\tport=%s\n", port_s);
    fflush(stdout);

    while (1) {
        SOCKET csock = accept(lsock, NULL, NULL);
        if (csock == INVALID_SOCKET)
            break;

        /* Connect to the Flinstone server through Windows→WSL2 loopback or
         * an explicit VM/guest target. */
        SOCKET wsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (wsock == INVALID_SOCKET) {
            closesocket(csock);
            continue;
        }
        struct sockaddr_in wsl_addr;
        memset(&wsl_addr, 0, sizeof(wsl_addr));
        wsl_addr.sin_family      = AF_INET;
        wsl_addr.sin_addr.s_addr = inet_addr(target);
        if (wsl_addr.sin_addr.s_addr == INADDR_NONE) {
            closesocket(wsock);
            closesocket(csock);
            continue;
        }
        wsl_addr.sin_port        = htons((USHORT)port);
        if (connect(wsock, (struct sockaddr *)&wsl_addr, sizeof(wsl_addr)) == SOCKET_ERROR) {
            closesocket(wsock);
            closesocket(csock);
            continue;
        }

        relay_pair *pair = reinterpret_cast<relay_pair *>(malloc(sizeof(relay_pair)));
        if (!pair) {
            closesocket(wsock);
            closesocket(csock);
            continue;
        }
        pair->a    = csock;
        pair->b    = wsock;
        pair->refs = 2;

        relay_task *t1 = reinterpret_cast<relay_task *>(malloc(sizeof(relay_task)));
        if (!t1) {
            free(pair);
            closesocket(wsock);
            closesocket(csock);
            continue;
        }
        t1->src = csock; t1->dst = wsock; t1->pair = pair;

        HANDLE h1 = CreateThread(NULL, 0, relay_thread, t1, 0, NULL);
        if (!h1) {
            free(t1);
            free(pair);
            closesocket(csock);
            closesocket(wsock);
            continue;
        }
        CloseHandle(h1); /* detach — relay_thread manages lifetime via refs */

        relay_task *t2 = reinterpret_cast<relay_task *>(malloc(sizeof(relay_task)));
        if (!t2) {
            /* t1 thread is running; let it finish its half; manually complete cleanup */
            if (InterlockedDecrement(&pair->refs) == 0) {
                closesocket(pair->a);
                closesocket(pair->b);
                free(pair);
            }
            continue;
        }
        t2->src = wsock; t2->dst = csock; t2->pair = pair;

        HANDLE h2 = CreateThread(NULL, 0, relay_thread, t2, 0, NULL);
        if (!h2) {
            free(t2);
            if (InterlockedDecrement(&pair->refs) == 0) {
                closesocket(pair->a);
                closesocket(pair->b);
                free(pair);
            }
            continue;
        }
        CloseHandle(h2);
    }

    closesocket(lsock);
    WSACleanup();
#else
    (void)bind_ip; (void)port_s; (void)target_ip;
    puts("result=err\tmsg=server-bridge only available on Windows");
#endif
}

/* -------------------------------------------------------------------------
 * server-proxy / server-proxy-del
 * Windows netsh portproxy + inbound firewall rule so LAN peers can reach a
 * TCP server bound inside WSL.  listen_ip is the address the user passed to
 * `server host <local-ip>:<port>` (often the Windows Wi-Fi IP); wsl_ip is
 * the WSL eth0 address from `hostname -I`.  Elevates via cmd.exe RunAs
 * when the current process is not already administrator.
 * ---------------------------------------------------------------------- */

#if defined(_WIN32)
static bool process_is_elevated(void) {
    BOOL        elevated = FALSE;
    HANDLE      token    = NULL;
    TOKEN_ELEVATION te;
    DWORD       sz       = sizeof(te);

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    if (GetTokenInformation(token, TokenElevation, &te, sizeof(te), &sz))
        elevated = te.TokenIsElevated;
    CloseHandle(token);
    return elevated != FALSE;
}

static int run_cmd_batch(const char *batch) {
    char cmdline[2304];

    if (process_is_elevated()) {
        snprintf(cmdline, sizeof(cmdline), "cmd.exe /c \"%s\"", batch);
        return system(cmdline);
    }
    {
        SHELLEXECUTEINFOA sei = {};
        char             params[2304];
        DWORD            exit_code = 1;

        snprintf(params, sizeof(params), "/c \"%s\"", batch);
        sei.cbSize       = sizeof(sei);
        sei.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        sei.lpVerb       = "runas";
        sei.lpFile       = "cmd.exe";
        sei.lpParameters = params;
        sei.nShow        = SW_HIDE;
        if (!ShellExecuteExA(&sei))
            return -1;
        if (!sei.hProcess)
            return -1;
        WaitForSingleObject(sei.hProcess, INFINITE);
        if (!GetExitCodeProcess(sei.hProcess, &exit_code))
            exit_code = 1;
        CloseHandle(sei.hProcess);
        return (int)exit_code;
    }
}

/* Parse one netsh portproxy v4tov4 data row; compare listen/connect port columns only
   (avoids false positives from IP octets like 192.168.1.80 matching port "80"). */
static int portproxy_line_has_port(const char *line, const char *port_s) {
    char listen_addr[64];
    char listen_port[16];
    char connect_addr[64];
    char connect_port[16];

    if (sscanf(line, " %63s %15s %63s %15s",
               listen_addr, listen_port, connect_addr, connect_port) != 4)
        return 0;
    if (strcmp(listen_addr, "Address") == 0)
        return 0;
    if (strchr(listen_addr, '-') != NULL)
        return 0;
    if (!is_valid_port(listen_port) || !is_valid_port(connect_port))
        return 0;
    return (strcmp(listen_port, port_s) == 0 || strcmp(connect_port, port_s) == 0);
}

static int portproxy_table_has_port(const char *port_s) {
    FILE *fp = _popen("netsh interface portproxy show v4tov4", "r");
    char  line[256];

    if (!fp)
        return 0;
    while (fgets(line, sizeof(line), fp)) {
        if (portproxy_line_has_port(line, port_s)) {
            (void)_pclose(fp);
            return 1;
        }
    }
    (void)_pclose(fp);
    return 0;
}

static int flinstone_firewall_rule_exists(const char *port_s) {
    char  cmd[384];
    char  line[512];
    FILE *fp;
    int   found = 0;

    snprintf(cmd, sizeof(cmd),
             "netsh advfirewall firewall show rule name=\"Flinstone Server %s\"",
             port_s);
    fp = _popen(cmd, "r");
    if (!fp)
        return 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "No rules match") != NULL)
            break;
        if (strstr(line, "Flinstone Server") != NULL)
            found = 1;
    }
    (void)_pclose(fp);
    return found;
}
#endif /* _WIN32 */

static void cmd_server_proxy_check(const char *listen_ip, const char *port_s) {
#if defined(_WIN32)
    const char *listen = (listen_ip && listen_ip[0]) ? listen_ip : "0.0.0.0";

    if (!is_valid_ipv4(listen) || !is_valid_port(port_s)) {
        puts("result=err\tmsg=invalid IP address or port number");
        return;
    }

    if (portproxy_table_has_port(port_s) || flinstone_firewall_rule_exists(port_s))
        printf("result=busy\tlisten=%s\tport=%s\n", listen, port_s);
    else
        printf("result=ok\tlisten=%s\tport=%s\n", listen, port_s);
#else
    (void)listen_ip;
    (void)port_s;
    puts("result=err\tmsg=server-proxy-check only available on Windows");
#endif
}

static void cmd_server_proxy(const char *listen_ip, const char *wsl_ip,
                             const char *port_s) {
#if defined(_WIN32)
    const char *listen = (listen_ip && listen_ip[0]) ? listen_ip : "0.0.0.0";
    char        batch[1024];
    int         rc;

    if (!is_valid_ipv4(listen) || !is_valid_ipv4(wsl_ip) || !is_valid_port(port_s)) {
        puts("result=err\tmsg=invalid IP address or port number");
        return;
    }

    /* netsh only — avoids slow PowerShell module load (New-NetFirewallRule). */
    snprintf(batch, sizeof(batch),
             "netsh interface portproxy add v4tov4 "
             "listenaddress=%s listenport=%s connectaddress=%s connectport=%s && "
             "netsh advfirewall firewall add rule name=\"Flinstone Server %s\" "
             "dir=in action=allow protocol=TCP localport=%s",
             listen, port_s, wsl_ip, port_s, port_s, port_s);
    rc = run_cmd_batch(batch);
    if (rc == 0)
        printf("result=ok\tlisten=%s\tport=%s\twsl_ip=%s\n", listen, port_s, wsl_ip);
    else
        printf("result=err\tmsg=portproxy/firewall failed (rc=%d, approve UAC if prompted)\n",
               rc);
#else
    (void)listen_ip;
    (void)wsl_ip;
    (void)port_s;
    puts("result=err\tmsg=server-proxy only available on Windows");
#endif
}

static void cmd_server_proxy_del(const char *listen_ip, const char *port_s) {
#if defined(_WIN32)
    const char *listen = (listen_ip && listen_ip[0]) ? listen_ip : "0.0.0.0";
    char        batch[768];
    int         rc;

    snprintf(batch, sizeof(batch),
             "netsh interface portproxy delete v4tov4 "
             "listenaddress=%s listenport=%s & "
             "netsh advfirewall firewall delete rule name=\"Flinstone Server %s\"",
             listen, port_s, port_s);
    rc = run_cmd_batch(batch);
    if (rc == 0)
        printf("result=ok\tlisten=%s\tport=%s\n", listen, port_s);
    else
        printf("result=err\tmsg=portproxy/firewall delete failed (rc=%d)\n", rc);
#else
    (void)listen_ip;
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
                "  server-bridge [<network>] <port> [target]  Relay LAN:<port> → target:<port>\n"
                "  server-proxy [<listen_ip>] <wsl_ip> <port>  Portproxy + firewall (UAC)\n"
                "  server-proxy-check [<listen_ip>] <port>     Probe portproxy/firewall\n"
                "  server-proxy-del [<listen_ip>] <port>       Remove portproxy + firewall\n");
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
    } else if (strcmp(cmd, "server-bridge") == 0) {
        if (argc < 3) {
            fprintf(stderr, "server-bridge: requires [<network>] <port>\n");
            return 1;
        }
        /* server-bridge <port>, server-bridge <bind_ip> <port>,
         * or server-bridge <bind_ip> <port> <target_ip>. */
        if (argc >= 5)
            cmd_server_bridge(argv[2], argv[3], argv[4]);
        else if (argc >= 4)
            cmd_server_bridge(argv[2], argv[3], NULL);
        else
            cmd_server_bridge(NULL, argv[2], NULL);
    } else if (strcmp(cmd, "server-proxy-check") == 0) {
        if (argc >= 4)
            cmd_server_proxy_check(argv[2], argv[3]);
        else if (argc >= 3)
            cmd_server_proxy_check("0.0.0.0", argv[2]);
        else {
            fprintf(stderr, "server-proxy-check: requires [<listen_ip>] <port>\n");
            return 1;
        }
    } else if (strcmp(cmd, "server-proxy") == 0) {
        if (argc >= 5)
            cmd_server_proxy(argv[2], argv[3], argv[4]);
        else if (argc >= 4)
            cmd_server_proxy("0.0.0.0", argv[2], argv[3]);
        else {
            fprintf(stderr, "server-proxy: requires [<listen_ip>] <wsl_ip> <port>\n");
            return 1;
        }
    } else if (strcmp(cmd, "server-proxy-del") == 0) {
        if (argc >= 4)
            cmd_server_proxy_del(argv[2], argv[3]);
        else if (argc >= 3)
            cmd_server_proxy_del("0.0.0.0", argv[2]);
        else {
            fprintf(stderr, "server-proxy-del: requires [<listen_ip>] <port>\n");
            return 1;
        }
    } else {
        fprintf(stderr, "FlinstonePowershell: unknown command '%s'\n", cmd);
        return 1;
    }
    return 0;
}
