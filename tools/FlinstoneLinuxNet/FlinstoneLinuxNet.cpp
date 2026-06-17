/*
 * FlinstoneLinuxNet — native Linux Wi-Fi/server helper for Bailey-Forbes-Flinstone.
 *
 * Protocol-compatible with the FlinstonePowershell helper:
 *   wifi-scan                         -> ssid=...\tbssid=...\trssi=...\tauth=...\tband=...\tchan=...
 *   wifi-join <ssid> [password]       -> result=ok\tssid=... or result=error\tmsg=...
 *   wifi-leave                        -> result=ok or result=error\tmsg=...
 *   wifi-status                       -> state=connected\tssid=...\tipv4=...\tprefix=...\tgateway=...
 *   server-bridge [bind_ip] <port> [target_ip]
 *
 * Wi-Fi uses NetworkManager's nmcli because it is widely available on desktop
 * and Raspberry Pi OS installs.  The shell still falls back to direct nmcli or
 * wpa_cli when this helper is absent.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

extern char **environ;

static std::string env_or_default(const char *name, const char *fallback)
{
    const char *v = getenv(name);
    return (v && v[0]) ? std::string(v) : std::string(fallback);
}

static std::string nmcli_path()
{
    return env_or_default("FL_NET_WIFI_NMCLI", "nmcli");
}

static std::string wifi_iface()
{
    const char *env = getenv("FL_NET_WIFI_IFACE");
    if (env && env[0])
        return std::string(env);

    FILE *fp = popen("nmcli -t -f DEVICE,TYPE device 2>/dev/null", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            char *nl = strpbrk(line, "\r\n");
            if (nl)
                *nl = '\0';
            char *colon = strchr(line, ':');
            if (!colon)
                continue;
            *colon = '\0';
            if (strcmp(colon + 1, "wifi") == 0 && line[0]) {
                std::string out(line);
                pclose(fp);
                return out;
            }
        }
        pclose(fp);
    }
    return "wlan0";
}

static std::vector<std::string> split_nmcli(const std::string &line)
{
    std::vector<std::string> parts;
    std::string cur;
    bool esc = false;
    for (char ch : line) {
        if (esc) {
            cur.push_back(ch);
            esc = false;
        } else if (ch == '\\') {
            esc = true;
        } else if (ch == ':') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    parts.push_back(cur);
    return parts;
}

static bool run_capture(const std::vector<std::string> &args, std::string *out)
{
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return false;

    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (const std::string &s : args)
        argv.push_back(const_cast<char *>(s.c_str()));
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);

    pid_t pid = 0;
    int rc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]);
    if (rc != 0) {
        close(pipefd[0]);
        return false;
    }

    if (out)
        out->clear();
    char buf[512];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        if (out)
            out->append(buf, (size_t)n);
    }
    close(pipefd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool run_status(const std::vector<std::string> &args)
{
    std::string ignored;
    return run_capture(args, &ignored);
}

static const char *auth_from_security(const std::string &sec)
{
    if (sec.find("WPA3") != std::string::npos || sec.find("SAE") != std::string::npos)
        return "wpa3";
    if (sec.find("WPA") != std::string::npos)
        return "wpa2";
    return "open";
}

static const char *band_from_freq(unsigned freq_mhz)
{
    if (freq_mhz >= 5900u)
        return "6";
    if (freq_mhz >= 4900u)
        return "5";
    return "2.4";
}

static int rssi_from_signal(int signal)
{
    if (signal <= 0)
        return -100;
    if (signal > 100)
        signal = 100;
    return signal / 2 - 100;
}

static void cmd_wifi_scan()
{
    std::string out;
    std::string iface = wifi_iface();
    std::vector<std::string> args = {
        nmcli_path(), "-t", "-f", "SSID,BSSID,SIGNAL,CHAN,FREQ,SECURITY",
        "device", "wifi", "list", "ifname", iface, "--rescan", "yes"
    };
    if (!run_capture(args, &out)) {
        fprintf(stderr, "FlinstoneLinuxNet: nmcli wifi scan failed on %s\n", iface.c_str());
        return;
    }

    size_t start = 0;
    while (start < out.size()) {
        size_t end = out.find('\n', start);
        std::string line = out.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = (end == std::string::npos) ? out.size() : end + 1;
        if (line.empty())
            continue;
        std::vector<std::string> p = split_nmcli(line);
        if (p.size() < 6 || p[0].empty())
            continue;
        int signal = atoi(p[2].c_str());
        unsigned chan = (unsigned)atoi(p[3].c_str());
        unsigned freq = (unsigned)atoi(p[4].c_str());
        printf("ssid=%s\tbssid=%s\trssi=%d\tauth=%s\tband=%s\tchan=%u\n",
               p[0].c_str(), p[1].c_str(), rssi_from_signal(signal),
               auth_from_security(p[5]), band_from_freq(freq), chan);
    }
}

static void cmd_wifi_join(const char *ssid, const char *password)
{
    std::string iface = wifi_iface();
    std::vector<std::string> args = { nmcli_path(), "device", "wifi", "connect", ssid, "ifname", iface };
    if (password && password[0]) {
        args.insert(args.end() - 2, "password");
        args.insert(args.end() - 2, password);
    }
    if (run_status(args))
        printf("result=ok\tssid=%s\n", ssid);
    else
        printf("result=error\tmsg=nmcli connect failed on %s\n", iface.c_str());
}

static void cmd_wifi_leave()
{
    std::string iface = wifi_iface();
    std::vector<std::string> args = { nmcli_path(), "device", "disconnect", iface };
    if (run_status(args))
        puts("result=ok");
    else
        printf("result=error\tmsg=nmcli disconnect failed on %s\n", iface.c_str());
}

static std::string field_value(const std::string &text, const char *key)
{
    std::string prefix = std::string(key) + ":";
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = (end == std::string::npos) ? text.size() : end + 1;
        if (line.rfind(prefix, 0) == 0)
            return line.substr(prefix.size());
    }
    return "";
}

static void cmd_wifi_status()
{
    std::string iface = wifi_iface();
    std::string out;
    std::vector<std::string> args = {
        nmcli_path(), "-t", "-f",
        "GENERAL.STATE,GENERAL.CONNECTION,IP4.ADDRESS,IP4.GATEWAY",
        "device", "show", iface
    };
    if (!run_capture(args, &out)) {
        puts("state=disconnected");
        return;
    }
    std::string state = field_value(out, "GENERAL.STATE");
    /* Match exact "connected" substring but exclude "disconnected": nmcli
     * GENERAL.STATE for a connected interface reads something like
     * "100 (connected)" while a disconnecting state reads "20 (disconnected)". */
    if (state.find("connected") == std::string::npos ||
        state.find("disconnected") != std::string::npos) {
        puts("state=disconnected");
        return;
    }
    std::string ssid = field_value(out, "GENERAL.CONNECTION");
    std::string addr = field_value(out, "IP4.ADDRESS[1]");
    std::string gw = field_value(out, "IP4.GATEWAY");
    std::string ip;
    std::string prefix = "24";
    size_t slash = addr.find('/');
    if (slash != std::string::npos) {
        ip = addr.substr(0, slash);
        prefix = addr.substr(slash + 1);
    } else {
        ip = addr;
    }
    if (!ip.empty())
        printf("state=connected\tssid=%s\tipv4=%s\tprefix=%s\tgateway=%s\n",
               ssid.c_str(), ip.c_str(), prefix.c_str(), gw.c_str());
    else
        printf("state=connected\tssid=%s\n", ssid.c_str());
}

static int bridge_relay(int a, int b)
{
    char buf[8192];
    while (1) {
        ssize_t n = recv(a, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = send(b, buf + off, (size_t)(n - off), 0);
            if (w <= 0)
                return -1;
            off += w;
        }
    }
    shutdown(b, SHUT_WR);
    return 0;
}

static void handle_client(int client, const char *target, uint16_t port)
{
    int upstream = socket(AF_INET, SOCK_STREAM, 0);
    if (upstream < 0) {
        close(client);
        _exit(1);
    }
    struct sockaddr_in dst {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    if (inet_pton(AF_INET, target, &dst.sin_addr) != 1 ||
        connect(upstream, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
        close(upstream);
        close(client);
        _exit(1);
    }

    pid_t child = fork();
    if (child == 0) {
        bridge_relay(client, upstream);
        close(client);
        close(upstream);
        _exit(0);
    }
    bridge_relay(upstream, client);
    close(client);
    close(upstream);
    if (child > 0)
        waitpid(child, nullptr, 0);
    _exit(0);
}

static int cmd_server_bridge(const char *bind_ip, const char *port_s, const char *target_ip)
{
    long port_l = strtol(port_s, nullptr, 10);
    if (port_l <= 0 || port_l > 65535) {
        printf("result=err\tmsg=invalid port\n");
        return 1;
    }
    const uint16_t port = (uint16_t)port_l;
    const char *bind_addr = (bind_ip && bind_ip[0]) ? bind_ip : "0.0.0.0";
    const char *target = (target_ip && target_ip[0]) ? target_ip : "127.0.0.1";

    signal(SIGCHLD, SIG_IGN);
    int lsock = socket(AF_INET, SOCK_STREAM, 0);
    if (lsock < 0) {
        printf("result=err\tmsg=socket failed (%d)\n", errno);
        return 1;
    }
    int yes = 1;
    setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (strcmp(bind_addr, "0.0.0.0") == 0)
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) {
        printf("result=err\tmsg=invalid bind address '%s'\n", bind_addr);
        close(lsock);
        return 1;
    }

    if (bind(lsock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("result=err\tmsg=bind failed (%d)\n", errno);
        close(lsock);
        return 1;
    }
    if (listen(lsock, 64) != 0) {
        printf("result=err\tmsg=listen failed (%d)\n", errno);
        close(lsock);
        return 1;
    }
    printf("result=ok\tbind=%s\tport=%u\ttarget=%s\n", bind_addr, (unsigned)port, target);
    fflush(stdout);

    while (1) {
        int client = accept(lsock, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        pid_t child = fork();
        if (child == 0) {
            close(lsock);
            handle_client(client, target, port);
        }
        close(client);
    }
    close(lsock);
    return 0;
}

static void usage()
{
    fputs("FlinstoneLinuxNet — native Linux Wi-Fi/server bridge\n"
          "\n"
          "Usage: FlinstoneLinuxNet <command> [args...]\n"
          "\n"
          "Commands:\n"
          "  platform                                Print platform=linux\n"
          "  wifi-scan                               List visible Wi-Fi networks via nmcli\n"
          "  wifi-join <ssid> [password]             Connect via nmcli\n"
          "  wifi-leave                              Disconnect FL_NET_WIFI_IFACE via nmcli\n"
          "  wifi-status                             Show nmcli connection/IP state\n"
          "  server-bridge [<network>] <port> [target]  Relay LAN:<port> -> target:<port>\n"
          "\n"
          "Env:\n"
          "  FL_NET_WIFI_IFACE       Wireless interface override\n"
          "  FL_NET_WIFI_NMCLI       nmcli path override\n",
          stderr);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 1;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "platform") == 0) {
        puts("platform=linux");
    } else if (strcmp(cmd, "wifi-scan") == 0) {
        cmd_wifi_scan();
    } else if (strcmp(cmd, "wifi-join") == 0) {
        if (argc < 3) {
            fputs("wifi-join: requires <ssid>\n", stderr);
            return 1;
        }
        cmd_wifi_join(argv[2], argc >= 4 ? argv[3] : "");
    } else if (strcmp(cmd, "wifi-leave") == 0) {
        cmd_wifi_leave();
    } else if (strcmp(cmd, "wifi-status") == 0) {
        cmd_wifi_status();
    } else if (strcmp(cmd, "server-bridge") == 0) {
        if (argc < 3) {
            fputs("server-bridge: requires [<network>] <port> [target]\n", stderr);
            return 1;
        }
        if (argc >= 5)
            return cmd_server_bridge(argv[2], argv[3], argv[4]);
        if (argc >= 4)
            return cmd_server_bridge(argv[2], argv[3], nullptr);
        return cmd_server_bridge(nullptr, argv[2], nullptr);
    } else {
        fprintf(stderr, "FlinstoneLinuxNet: unknown command '%s'\n", cmd);
        usage();
        return 1;
    }
    return 0;
}
