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

/*
 * Test doubles. cmd_netlab.c keys pod membership on the real logged-in account
 * (fl_session_current_user) and asks whether this process hosts the real
 * `server` session (cmd_server_is_running). Those live in session.o / cmd_server.o,
 * which this lightweight unit test does not link. Stubbing them here lets the
 * test drive identity and the server-host flag directly -- which is exactly how
 * we exercise two distinct participants on the hosted single-identity model.
 */
static char g_test_user[64] = "flinstone";
static char g_test_server_host[64] = "";   /* account hosting the server; "" = none */

void        fl_session_init(void) {}
const char *fl_session_current_user(void) { return g_test_user; }
int         cmd_server_is_running(void) { return g_test_server_host[0] != '\0'; }
int         cmd_server_is_connected(void) { return 0; }
const char *cmd_server_host_user(void) { return g_test_server_host; }

static void as_user(const char *u) {
    strncpy(g_test_user, u, sizeof(g_test_user) - 1);
    g_test_user[sizeof(g_test_user) - 1] = '\0';
}

/* Model `server host`/`kill`: record the account that owns the running server
 * (the real cmd_server.c captures this via set_server_running). "" clears it. */
static void set_server_host(const char *u) {
    strncpy(g_test_server_host, u, sizeof(g_test_server_host) - 1);
    g_test_server_host[sizeof(g_test_server_host) - 1] = '\0';
}

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

    /* pods: real-account membership, host-collision avoidance, succession.
     * Membership is keyed on fl_session_current_user() (stubbed above), never
     * on a free-typed name argument. */
    set_server_host("");
    as_user("flinstone");
    {
        char *av[] = { "netlab", "pod", "create", "redcell" };
        rc = run_capture(av, 4, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab pod create redcell` ok");
        CHECK(strstr(buf, "host is flinstone") != NULL,
              "pod creator (real account) becomes host when not the server host");
    }
    {
        /* Second real participant joins by switching identity, not by name. */
        char *av[] = { "netlab", "pod", "join" };
        as_user("root");
        rc = run_capture(av, 3, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab pod join` ok (as root)");
        CHECK(strstr(buf, "identity preserved") != NULL, "join preserves real identity");
    }
    {
        char *av[] = { "netlab", "pod", "members" };
        rc = run_capture(av, 3, buf, sizeof(buf));
        CHECK(strstr(buf, "flinstone") != NULL && strstr(buf, "root") != NULL,
              "members lists both real accounts");
        CHECK(strstr(buf, "(host)") != NULL, "members marks a host");
    }

    /* Host-collision: if the current identity is also hosting the real server,
     * it must not also hold the pod host role (host pending until someone else
     * can hold it). */
    {
        char *leave_root[] = { "netlab", "pod", "leave" };  /* root leaves   */
        char *leave_fl[]   = { "netlab", "pod", "leave" };  /* flinstone too */
        as_user("root");    run_capture(leave_root, 3, buf, sizeof(buf));
        as_user("flinstone"); run_capture(leave_fl, 3, buf, sizeof(buf));
    }
    {
        char *av[] = { "netlab", "pod", "create", "redcell" };
        set_server_host("flinstone");   /* flinstone is now the real server host */
        as_user("flinstone");
        rc = run_capture(av, 4, buf, sizeof(buf));
        CHECK(rc == 0, "`netlab pod create` ok while hosting server");
        CHECK(strstr(buf, "host pending") != NULL,
              "server host does not also become the pod host (host pending)");
    }
    set_server_host("");

    /* Reverse shell: listener = attacker, acceptor = victim. The attacker's
     * `netlab <cmd>` runs against a disposable sandbox labeled for the victim,
     * never the victim's real account. */
    {
        char *join[]   = { "netlab", "pod", "join" };
        char *listen[] = { "netlab", "listen" };
        as_user("root");                                  /* second member    */
        run_capture(join, 3, buf, sizeof(buf));
        as_user("flinstone");                             /* flinstone listens */
        rc = run_capture(listen, 2, buf, sizeof(buf));
        CHECK(rc == 0 && strstr(buf, "is listening") != NULL,
              "`netlab listen` opens a listener (attacker side)");
    }
    {
        char *self[] = { "netlab", "accept" };
        rc = run_capture(self, 2, buf, sizeof(buf));      /* flinstone again   */
        CHECK(rc != 0, "self-accept of your own listener is rejected");
    }
    {
        char *av[] = { "netlab", "accept" };
        as_user("root");                                  /* root accepts      */
        rc = run_capture(av, 2, buf, sizeof(buf));
        CHECK(rc == 0 && strstr(buf, "SANDBOX") != NULL,
              "`netlab accept` binds the acceptor as the victim sandbox side");
        CHECK(cmd_netlab_is_isolated() == 1,
              "the real network is isolated while a binding is open");
    }
    {
        /* The bound attacker (flinstone) runs real sandbox commands. */
        char *who[]   = { "netlab", "whoami" };
        char *wr[]    = { "netlab", "write", "loot.txt", "exfil" };
        char *rd[]    = { "netlab", "cat", "loot.txt" };
        as_user("flinstone");
        rc = run_capture(who, 2, buf, sizeof(buf));
        CHECK(rc == 0 && strstr(buf, "root") != NULL,
              "attacker `netlab whoami` reports the victim identity (sandbox)");
        run_capture(wr, 4, buf, sizeof(buf));
        rc = run_capture(rd, 3, buf, sizeof(buf));
        CHECK(rc == 0 && strstr(buf, "exfil") != NULL,
              "sandbox write/cat execute for real against isolated state");
    }
    {
        /* The non-bound member's own netlab verbs are NOT redirected. */
        char *who[] = { "netlab", "whoami" };
        as_user("root");   /* root is the victim, not the attacker */
        CHECK(run_capture(who, 2, buf, sizeof(buf)) != 0,
              "the victim's own `netlab whoami` is not hijacked (unknown verb)");
    }
    {
        /* The accepting victim can end the binding unilaterally. */
        char *end[] = { "netlab", "end" };
        as_user("root");
        rc = run_capture(end, 2, buf, sizeof(buf));
        CHECK(rc == 0 && strstr(buf, "closed") != NULL,
              "the victim can `netlab end` the binding unilaterally");
        CHECK(cmd_netlab_is_isolated() == 0, "ending the binding restores the real network");
    }
    {
        char *who[] = { "netlab", "whoami" };
        as_user("flinstone");
        CHECK(run_capture(who, 2, buf, sizeof(buf)) != 0,
              "`netlab whoami` is unknown once the binding is closed");
    }
    {
        /* Host succession: with flinstone gone as server host, root remains and
         * becomes the pod host. */
        char *leave[] = { "netlab", "pod", "leave" };
        char *members[] = { "netlab", "pod", "members" };
        as_user("flinstone");
        rc = run_capture(leave, 3, buf, sizeof(buf));
        CHECK(rc == 0, "host leaves the pod");
        as_user("root");
        run_capture(members, 3, buf, sizeof(buf));
        CHECK(strstr(buf, "root") != NULL && strstr(buf, "(host)") != NULL,
              "the remaining real member becomes the pod host");
    }
    /* Reset to a clean, single-member state for the remaining verb checks. */
    {
        char *leave[] = { "netlab", "pod", "leave" };
        as_user("root");
        run_capture(leave, 3, buf, sizeof(buf));
    }

    /* Server lifecycle coupling (the hooks cmd_server calls on leave/kill). */
    {
        char *create[]  = { "netlab", "pod", "create", "redcell" };
        char *join[]    = { "netlab", "pod", "join" };
        char *members[] = { "netlab", "pod", "members" };
        set_server_host("");
        as_user("flinstone");
        run_capture(create, 4, buf, sizeof(buf));   /* flinstone: pod host     */

        /* `server host`: flinstone (sole member) becomes the server host, which
         * is a separate relay point, so the pod host must move off flinstone --
         * with no other member it goes pending. */
        set_server_host("flinstone");
        cmd_netlab_on_server_host();
        run_capture(members, 3, buf, sizeof(buf));
        CHECK(strstr(buf, "flinstone  (host)") == NULL,
              "becoming the server host drops the pod host off that identity");

        /* root joins while flinstone is the server host. Election must exclude
         * flinstone (the captured server-host account) even though the current
         * identity is now root -- so root takes the pod host. */
        as_user("root");
        run_capture(join, 3, buf, sizeof(buf));
        run_capture(members, 3, buf, sizeof(buf));
        CHECK(strstr(buf, "root  (host)") != NULL,
              "server host account is excluded from pod host regardless of current identity");

        /* `server leave` by the host: the real verb_leave sets the server flag
         * off before calling the hook, so mirror that. Pod election then re-runs
         * and the first member by join order holds the host. */
        set_server_host("");
        cmd_netlab_on_server_leave();
        run_capture(members, 3, buf, sizeof(buf));
        CHECK(strstr(buf, "(host)") != NULL, "server leave re-elects a valid pod host");

        /* `server kill` dissolves the pod that lived under the server. */
        cmd_netlab_on_server_kill();
        run_capture(members, 3, buf, sizeof(buf));
        CHECK(strstr(buf, "no pod") != NULL, "server kill dissolves the pod");
        CHECK(cmd_netlab_is_isolated() == 0, "server kill leaves the real network restored");
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
