# netlab — contained network-security teaching lab

`netlab` is a userland shell command that reproduces the *concepts* from the
CSCI C 437 "Extending MiTM via Reverse Shells" project — ARP spoofing,
man-in-the-middle, reverse-shell mechanics, traceroute, traffic capture, and
pcap analysis — entirely inside the emulator, against a fabricated topology
that lives only in process memory.

Source: `userland/command/cmd_netlab.c`. Test: `tests/test_netlab_sim.c`
(`make test_netlab_sim`, also run by `scripts/run_all_tests.sh`).

## Design boundary (why this is safe to ship)

`netlab` is built to be incapable of live-attack behavior, not merely
discouraged from it:

- It is **libc-only** and pulls in no kernel net headers. It opens no sockets
  and reads no real interface.
- It never intercepts, relays, or alters another host's real traffic.
- It never sends input into another person's terminal. The listen/accept
  reverse-shell binding always drives a **disposable, in-memory sandbox**
  labeled for the accepting account — never that account's real session, real
  files, or real terminal. The accepting party can end the binding at any time.
- It never rewrites a real capture to fabricate evidence of a MiTM.

The only real I/O is files: `pcap write` serializes the **simulated** frame
script to a standard libpcap file (stamped as simulated), and `pcap analyze`
reads a real pcap you already have and reports on it. Reading and explaining a
capture — including raising the duplicate-MAC alert that reveals ARP spoofing —
is ordinary defensive forensics.

This is a deliberate scoping decision. A working MiTM interceptor, a reverse
shell that executes an operator's commands on someone else's **real** machine
or session, live reconnaissance of other users, and capture forgery are
live-attack capabilities whose code is identical whoever the target is;
restricting their *use* to a lab does not change what the code *is*, so they
are out of scope here. The listen/accept binding deliberately targets a
throwaway sandbox instead, so the attacker's workflow is demonstrated without
any real session ever being the victim. The lab teaches the mechanism and, just as importantly, the detection
and prevention half of the source report — the part that generalizes.

## Pods

Pods are private, isolated groups: a pod's simulated hosts and frames never
cross into another pod. Two ship by default:

| pod | name          | ssid           | hosts |
|-----|---------------|----------------|-------|
| 1   | `redteam-lab` | `FLINT-LAB-AP` | gateway, alice, bob, observer |
| 2   | `blueteam-lab`| `FLINT-BLUE-AP`| gateway, carol, ids |

The `observer` host is the vantage point a MiTM (or an IDS sensor) would
occupy; it is where the simulated spoof originates in the frame script.

## Verbs

```
netlab consent                 Show the lab-only boundary this tool enforces
netlab pods                    List simulated pods
netlab hosts <pod>             List a pod's simulated hosts (SSID/IP/MAC/role)
netlab scan <pod>              Host discovery (nmap -sn style): up hosts + MACs
netlab wpa <pod>               Show the pod AP's simulated security + PSK
netlab topology <pod>          ASCII map of a pod
netlab traceroute <pod> <dst>  Simulated hop path to a host in the pod
netlab monitor <pod> [-c N]    Replay the pod's simulated frame script
netlab arp-demo <pod>          Walk through how ARP-spoof MiTM works (illustration)
netlab rs-explain              Explain reverse-shell mechanics (no injection)
netlab pcap write <pod> <file> Emit a real .pcap of the SIMULATED frames
netlab pcap analyze <file>     Parse a real .pcap; flag ARP-spoof signatures
netlab defend                  Detection & prevention checklist
netlab pod create|join|members|host|leave   Pod membership, keyed on your real account (see Pods below)
netlab listen|accept|end                    Simulated reverse shell: listener=attacker, acceptor=victim (see below)
netlab status                  Show pod / host / reverse-shell state
```

## How it maps to the seven asks in the report

| Asked for | What `netlab` does |
|---|---|
| Connect to the server | Out of scope as a live action; the pod model stands in for a shared session, simulated in memory. |
| Monitor traffic into a pcap | `monitor` replays the simulated frame script; `pcap write` serializes it to a real libpcap file (clearly simulated data). |
| Device/network info for others | `hosts` shows each pod's **fabricated** host table (SSID/IP/MAC/role). No real device is probed. |
| Traceroute | `traceroute` walks the simulated pod topology hop by hop. |
| Send commands to another's terminal (reverse shell) | `netlab listen` (attacker) + another member's `netlab accept` (victim) bind a session; the attacker's `netlab <cmd>` then runs against a **disposable sandbox** labeled for the victim (simulation); `rs-explain` covers the mechanism. No real participant's session is controlled. |
| Analysis of a pcap file | `pcap analyze` parses a real capture, counts frames, and flags duplicate IP->MAC bindings (the ARP-spoof signature). |
| Alter router state to show MiTM in a pcap | Not implemented — fabricating capture evidence is out of scope. The analyzer teaches how such tampering is *detected*. |

## Try it

```
netlab pods
netlab arp-demo redteam-lab
netlab pcap write redteam-lab /tmp/demo.pcap
netlab pcap analyze /tmp/demo.pcap     # raises the ARP-conflict alert
netlab defend
```

## Reproduction coverage (CSCI C 437 report)

The report's "Commands and Operations for Reproduction" section maps onto
netlab as follows. Every row runs on the fabricated pod topology — no real
host, network, or saved credential is touched.

| Report step | netlab |
|---|---|
| Check permissions / elevate (`whoami`, `sudo su`) | the shell's own `whoami`, `sudo`, `su` |
| Get WPA key (`grep psk= …NetworkManager…`) + SSID security | `netlab wpa <pod>` — fabricated PSK + key-mgmt for the pod AP |
| Show network connections / security settings | `netlab wpa <pod>`, `netlab hosts <pod>` |
| Local wireless info (`ifconfig`) | shell `ifconfig` / `netlab hosts` |
| Traceroute + IP details | `netlab traceroute <pod> <dst>` |
| `nmap -sn` host discovery → MAC + IP of devices | `netlab scan <pod>` |
| Network topology | `netlab topology <pod>` |
| MiTM via Ettercap + tcpdump → pcap | `netlab arp-demo` / `monitor` / `capture`; hosted `pcap write` |
| Reverse shell (`ncat -e /bin/bash`) | `netlab listen` / `netlab accept` bind attacker↔victim; the attacker's `netlab <cmd>` runs in a disposable sandbox, `netlab end` closes it; `rs-explain` for the mechanism. No real victim session. |
| pcap analysis in Wireshark | hosted `netlab pcap analyze <file>`; freestanding `netlab capture` |
| Prevention strategies | `netlab defend` |

The `wpa` PSK and the `scan` results are fabricated values in the pod
definitions. netlab does not read `/etc/NetworkManager`, capture a handshake,
or crack anything, and it does not implement a reverse shell into a real
session — those are live-attack steps and stay out of scope by design.

## Pods and the simulated reverse-shell session

netlab keeps a small amount of session state so a pod and a reverse shell can
be modeled the way the course project describes. Pod membership is keyed on
your **real logged-in account**, not a free-typed name. The reverse shell
binds two real accounts but drives a disposable in-memory sandbox, never a
real participant's session.

### Pods

```
netlab pod create <pod>   create a pod; you (your real account) are the first member
netlab pod join           join the pod as your real account
netlab pod members        list members (real accounts); the host is marked
netlab pod host           show the current pod host
netlab pod leave          leave the pod; host succeeds if the host leaves
```

Membership reflects the real logged-in account (`fl_session_current_user()` in
the hosted shell, the real per-terminal session in the freestanding shell), so
adding a second member means a second real account joining — in the browser
lab, a second session opened as another account.

**Host-collision avoidance.** The pod host is a member that is **not** also
hosting the real `server` session, so one identity is never asked to relay both
lab traffic and real-server traffic at once. If every current member is the real
server host, the pod host stays **pending** until another account can hold it.
This is re-resolved at pod create/join/leave and, crucially, again at
`accept` time — the moment the lab network is switched on.

In the freestanding shell, which has a real per-terminal session table, two
sessions logged into the same account are disambiguated for display as
`name` and `name {2}`. The hosted ELF shell has a single process-global
identity (no concurrent session table), so it keys membership on whichever
real account is logged in at the moment each verb runs; this is a weaker
concurrency model and is documented as such rather than overclaimed.

**Server lifecycle coupling.** The real `server` and netlab pods interact
through two lifecycle events, mirroring the distinction between the server's
own `leave` (host steps down, the server continues) and `kill` (the server is
torn down):

- **`server kill` by the server host → the pods under it are killed.** Killing
  the server dissolves the pod and any open binding and restores the real
  network. (Hosted: `cmd_server_run` → `cmd_netlab_on_server_kill`. Freestanding:
  `run_server` → `nl_pod_dissolve`.)
- **`server leave` by a host that is also a pod host → both re-elect, independently.**
  The server picks a new host by *its* join order; netlab re-elects the pod host
  by *pod* join order. Because the two orderings differ, the new server host and
  the new pod host can be different people — which is the intended behaviour.
  (Hosted: `cmd_netlab_on_server_leave`. Freestanding: `nl_pick_host` from the
  `server leave` path.)

A binding pauses the real network, so `server kill`/`leave` are only reachable
when no binding is open (otherwise `netlab end` comes first); the coupling
therefore applies to a live pod rather than an active reverse shell. On the
freestanding side the relay performs the server's own host succession hub-side,
so the shell forwards `leave` and only re-elects the *pod* host locally.

### Reverse shell (simulated): listen / accept

```
netlab listen          open a listener; you become the attacker
netlab listen cancel   withdraw your own open listener
netlab accept          accept the open listener; you become the victim
netlab <cmd>           while bound, the ATTACKER's non-mgmt verbs run in the sandbox
netlab end             close the binding (either side, any time)
netlab status          show pod / host / reverse-shell / network state
```

Whoever runs `netlab listen` is the **attacker**; whoever runs `netlab accept`
is the **victim**. You cannot accept your own listener. Once bound, the
attacker's non-management `netlab <cmd>` is redirected into a fresh, isolated
**sandbox** labeled for the victim — the sandbox runs real commands
(`whoami`, `hostname`, `id`, `uname`, `pwd`, `ifconfig`, `netstat`, `ls`,
`write`, `cat`, `rm`) against its own throwaway state, never the victim's real
account, files, or terminal. The victim keeps using their own real shell
normally; only the attacker's `netlab` verbs are redirected.

Worked example — Flinstone listens, root accepts:

```
# session A (flinstone)
netlab pod create redcell
netlab listen               -> flinstone is listening (attacker)

# session B (root)
netlab pod join
netlab accept               -> root bound as the victim SANDBOX side

# session A (flinstone, the attacker)
netlab whoami               -> root               (the sandbox's labeled identity)
netlab write loot.txt data  -> wrote loot.txt (sandbox)
netlab cat loot.txt         -> data

# session B (root, the victim) — its own shell is untouched
whoami                      -> root
netlab end                  -> binding closed; real network restored
```

Accepting the listener drops the real network for the duration (the hosted
`server` command refuses while isolated via `cmd_netlab_is_isolated()`, the
freestanding `server` relay refuses, and the browser glue disconnects the relay
on the `LAB_NETMODE lab` token); `netlab end` — which either side may run —
restores it. This demonstrates the attacker's experience of a reverse shell
without controlling any real person's machine or session.

## In the browser lab (freestanding) and the non-CLI controls

The portfolio embeds the freestanding browser kernel, not the hosted ELF
shell, so `netlab` also ships as a freestanding analog in
`kernel/freestanding/x86_64/commands.c`. It has no libc and no file I/O, so it
drops the binary pcap file verbs and instead offers `netlab capture <pod>`,
which prints the simulated frames and raises the same ARP-conflict alert
in-line. The other verbs (`pods`, `hosts`, `scan`, `wpa`, `topology`,
`traceroute`, `monitor`, `arp-demo`, `rs-explain`, `defend`, `consent`) match the hosted
command. It remains simulation-only: fabricated hosts, no sockets, no real
interface, no interception, no injection.

### Lab vs live: dropping the real network for a demo

The freestanding lab adds a network-mode switch:

- `netlab start` enters **lab-isolated** mode. It emits a `LAB_NETMODE lab`
  control line on the serial port, and while isolated the freestanding
  `server` relay verbs refuse — so nothing leaves the lab during the MitM+RS
  demo.
- `netlab stop` returns to **live** mode (`LAB_NETMODE live`); the server
  relay works again.
- `netlab status` reports the current mode.

In the browser, the "real network" is the chat relay (a Cloudflare Durable
Object WebSocket hub, with a same-origin BroadcastChannel fallback). The glue
in `tools/browser-lab/lab.js` watches for the `LAB_NETMODE` token (parsed by
`lab-core.js`): on `lab` it disconnects the relay and suppresses
auto-reconnect; on `live` it reconnects. This scopes "drop the real network"
to the lab's own relay connection — it never touches the host's actual Wi-Fi.

### Driving it without the CLI

`tools/browser-lab/index.html` has a **MitM + RS lab** control panel: a segment
selector, a live/lab status badge, recon buttons (Hosts, Scan, WPA, Topology,
ARP-spoof demo, Capture + analyze), pod buttons (Create pod, Join pod as me,
Members, Leave pod), reverse-shell buttons (Listen as attacker, Accept as
victim, End binding, Status), and the network/reference buttons (Start demo,
Stop demo, Consent, RS mechanics, Defend). The pod and reverse-shell buttons
carry no free-typed name — membership follows the session's real account, so a
second participant is a second session opened as another account in the Users
and sessions panel. Each button sends the matching `netlab …` line into the
shell, so the whole flow runs by clicking, no typing required.

## Related

- `docs/browser-kernel-lab.md` — the freestanding browser kernel and its lab shell.
- `docs/P3_NETWORKING.md` — the real networking stack (sockets, ARP, routing).
- `docs/P3_13_CHAT_SERVER.md` / `docs/SERVER.md` — the real multi-party server.
- The analyzer accepts classic libpcap in either byte order and microsecond or
  nanosecond timestamps; it skips non-Ethernet link types and bounds-checks
  every record, so it is safe to point at arbitrary capture files.
