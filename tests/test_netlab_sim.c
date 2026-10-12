/*
 * test_netlab_sim.c — unit test for the `netlab` teaching lab (cmd_netlab.c).
 *
 * Covers: command registration, the simulation verbs, the simulated->pcap
 * round trip, and — the point of the defensive half — that analyzing the
 * emitted capture raises the duplicate-MAC ARP-spoof alert. Everything here
 * runs in-process on fabricated data; no sockets, no real interfaces.
 */

#define _POSIX_C_SOURCE 200809L

#include "cmd_decl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  ok  : %s\n", msg);                                       \
        } else {                                                               \
            printf("  FAIL: %s\n", msg);                                       \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* Run cmd_netlab_run with stdout captured to capbuf; returns its exit code. */
static int run_capture(char **argv, int argc, char *capbuf, size_t cap) {
    const char *cappath = "/tmp/netlab_test_capture.txt";
    int rc, saved;
    FILE *cf;
    FILE *rf;
    size_t got;

    fflush(stdout);
    saved = dup(fileno(stdout));
    cf = fopen(cappath, "w+");
    if (!cf || saved < 0) {
        if (cf) fclose(cf);
        return cmd_netlab_run(argc, argv); /* best effort, no capture */
    }
    dup2(fileno(cf), fileno(stdout));
    rc = cmd_netlab_run(argc, argv);
    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);
    fclose(cf);

    capbuf[0] = '\0';
    rf = fopen(cappath, "rb");
    if (rf) {
        got = fread(capbuf, 1, cap - 1, rf);
        capbuf[got] = '\0';
        fclose(rf);
    }
    remove(cappath);
    return rc;
}

int main(void) {
    char buf[8192];
    const char *pcap_path = "/tmp/netlab_test_out.pcap";
    int rc;

    printf("test_netlab_sim:\n");

    /* 1. Registration */
    CHECK(fl_shell_cmd_lookup("netlab") == FL_SCMD_NETLAB,
          "netlab resolves to FL_SCMD_NETLAB");
    CHECK(fl_shell_cmd_lookup("netlabx") == FL_SCMD_UNKNOWN,
          "unknown name does not resolve");

    /* 2. Informational verbs return 0 */
    {
        char *av[] = { "netlab", "pods" };
        CHECK(run_capture(av, 2, buf, sizeof(buf)) == 0, "`netlab pods` ok");
        CHECK(strstr(buf, "redteam-lab") != NULL, "pods lists redteam-lab");
        CHECK(strstr(buf, "blueteam-lab") != NULL, "pods lists blueteam-lab");
    }
    {
        char *av[] = { "netlab", "consent" };
        rc = run_capture(av, 2, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab consent` ok");
        CHECK(strstr(buf, "NOT") != NULL || strstr(buf, "not") != NULL,
              "consent states the no-real-network boundary");
    }
    {
        char *av[] = { "netlab", "hosts", "redteam-lab" };
        rc = run_capture(av, 3, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab hosts redteam-lab` ok");
        CHECK(strstr(buf, "192.168.56.1") != NULL, "hosts shows gateway IP");
        CHECK(strstr(buf, "observer") != NULL, "hosts shows observer role");
    }
    {
        char *av[] = { "netlab", "hosts", "no-such-pod" };
        CHECK(run_capture(av, 3, buf, sizeof(buf)) != 0, "unknown pod fails");
    }

    /* scan (nmap -sn style host discovery -> MAC addresses) */
    {
        char *av[] = { "netlab", "scan", "redteam-lab" };
        rc = run_capture(av, 3, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab scan redteam-lab` ok");
        CHECK(strstr(buf, "02:00:00:00:56:01") != NULL, "scan reports a MAC address");
        CHECK(strstr(buf, "host up") != NULL, "scan reports hosts up");
    }

    /* wpa (simulated AP credentials; fabricated, no real config) */
    {
        char *av[] = { "netlab", "wpa", "redteam-lab" };
        rc = run_capture(av, 3, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab wpa redteam-lab` ok");
        CHECK(strstr(buf, "FLINT-LAB-AP") != NULL, "wpa shows the ssid");
        CHECK(strstr(buf, "fabricated") != NULL, "wpa marks the psk as fabricated");
        CHECK(strstr(buf, "crack") != NULL, "wpa states it cracks nothing");
    }

    /* pods: membership, preserved identity, host succession */
    {
        char *av[] = { "netlab", "pod", "create", "redcell", "jenny" };
        rc = run_capture(av, 5, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab pod create redcell jenny` ok");
        CHECK(strstr(buf, "host is jenny") != NULL, "pod creator becomes host");
    }
    {
        char *av[] = { "netlab", "pod", "join", "flinstone" };
        rc = run_capture(av, 4, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab pod join flinstone` ok");
        CHECK(strstr(buf, "identity preserved") != NULL, "join preserves identity");
    }
    {
        char *av[] = { "netlab", "pod", "members" };
        rc = run_capture(av, 3, buf, sizeof(buf));
        CHECK(strstr(buf, "jenny") != NULL && strstr(buf, "(host)") != NULL,
              "members lists the host");
    }

    /* reverse shell: netlab <cmd> runs as the mock victim; netlab end closes */
    {
        char *av[] = { "netlab", "rs", "bob" };
        rc = run_capture(av, 3, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab rs bob` opens a reverse shell");
        CHECK(strstr(buf, "SIMULATED") != NULL, "rs marks the victim simulated");
    }
    {
        char *av[] = { "netlab", "whoami" };
        rc = run_capture(av, 2, buf, sizeof(buf));
        CHECK(rc == 0 && strstr(buf, "bob") != NULL,
              "`netlab whoami` returns the victim's mock name while RS open");
    }
    {
        char *av[] = { "netlab", "hostname" };
        run_capture(av, 2, buf, sizeof(buf));
        CHECK(strstr(buf, "bob-box") != NULL, "`netlab hostname` returns victim hostname");
    }
    {
        char *av[] = { "netlab", "end" };
        rc = run_capture(av, 2, buf, sizeof(buf));
        CHECK(rc == 0 && strstr(buf, "closed") != NULL, "`netlab end` closes the RS");
    }
    {
        char *av[] = { "netlab", "whoami" };
        CHECK(run_capture(av, 2, buf, sizeof(buf)) != 0,
              "`netlab whoami` is unknown once the RS is closed");
    }
    {
        char *av[] = { "netlab", "pod", "leave", "jenny" };
        rc = run_capture(av, 4, buf, sizeof(buf));
        CHECK(rc == 0 && strstr(buf, "new host is flinstone") != NULL,
              "host succession when the host leaves");
    }

    /* 3. traceroute over the simulated topology */
    {
        char *av[] = { "netlab", "traceroute", "redteam-lab", "bob" };
        rc = run_capture(av, 4, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab traceroute redteam-lab bob` ok");
        CHECK(strstr(buf, "bob") != NULL, "traceroute reaches bob");
    }
    {
        char *av[] = { "netlab", "traceroute", "redteam-lab", "nobody" };
        CHECK(run_capture(av, 4, buf, sizeof(buf)) != 0,
              "traceroute to unknown host fails");
    }

    /* 4. monitor + arp-demo run */
    {
        char *av[] = { "netlab", "monitor", "redteam-lab" };
        rc = run_capture(av, 3, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab monitor redteam-lab` ok");
        CHECK(strstr(buf, "SPOOF") != NULL, "monitor script includes the spoof frame");
    }
    {
        char *av[] = { "netlab", "arp-demo", "redteam-lab" };
        CHECK(run_capture(av, 3, buf, sizeof(buf)) == 0, "`netlab arp-demo` ok");
    }

    /* 5. pcap write -> analyze round trip, with ARP-spoof detection */
    {
        char *av[] = { "netlab", "pcap", "write", "redteam-lab", (char *)pcap_path };
        rc = run_capture(av, 5, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab pcap write` ok");
        /* file exists and starts with the little-endian pcap magic */
        FILE *pf = fopen(pcap_path, "rb");
        unsigned char magic[4] = { 0 };
        CHECK(pf != NULL, "pcap file created");
        if (pf) {
            size_t n = fread(magic, 1, 4, pf);
            fclose(pf);
            CHECK(n == 4 && magic[0] == 0xd4 && magic[1] == 0xc3 &&
                      magic[2] == 0xb2 && magic[3] == 0xa1,
                  "pcap file has little-endian classic magic");
        }
    }
    {
        char *av[] = { "netlab", "pcap", "analyze", (char *)pcap_path };
        rc = run_capture(av, 4, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab pcap analyze` ok");
        CHECK(strstr(buf, "ALERT") != NULL, "analyze raises an ALERT");
        CHECK(strstr(buf, "ARP conflict") != NULL, "analyze names the ARP conflict");
        CHECK(strstr(buf, "arp:") != NULL, "analyze prints a frame summary");
    }
    remove(pcap_path);

    /* 6. error paths */
    {
        char *av[] = { "netlab", "pcap", "analyze", "/no/such/file.pcap" };
        CHECK(run_capture(av, 4, buf, sizeof(buf)) != 0,
              "analyze of a missing file fails");
    }
    {
        char *av[] = { "netlab", "bogus-verb" };
        CHECK(run_capture(av, 2, buf, sizeof(buf)) != 0, "unknown verb fails");
    }

    printf("test_netlab_sim: %s (%d failure%s)\n",
           failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
