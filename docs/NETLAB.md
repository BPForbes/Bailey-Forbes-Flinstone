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
- It never sends input into another person's terminal — there is **no reverse
  shell into a victim**, only an explainer of how the technique works.
- It never rewrites a real capture to fabricate evidence of a MiTM.

The only real I/O is files: `pcap write` serializes the **simulated** frame
script to a standard libpcap file (stamped as simulated), and `pcap analyze`
reads a real pcap you already have and reports on it. Reading and explaining a
capture — including raising the duplicate-MAC alert that reveals ARP spoofing —
is ordinary defensive forensics.

This is a deliberate scoping decision. A working MiTM interceptor, a reverse
shell that executes an operator's commands on someone else's machine, live
reconnaissance of other users, and capture forgery are live-attack
capabilities whose code is identical whoever the target is; restricting their
*use* to a lab does not change what the code *is*, so they are out of scope
here. The lab teaches the mechanism and, just as importantly, the detection
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
netlab topology <pod>          ASCII map of a pod
netlab traceroute <pod> <dst>  Simulated hop path to a host in the pod
netlab monitor <pod> [-c N]    Replay the pod's simulated frame script
netlab arp-demo <pod>          Walk through how ARP-spoof MiTM works (illustration)
netlab rs-explain              Explain reverse-shell mechanics (no injection)
netlab pcap write <pod> <file> Emit a real .pcap of the SIMULATED frames
netlab pcap analyze <file>     Parse a real .pcap; flag ARP-spoof signatures
netlab defend                  Detection & prevention checklist
```

## How it maps to the seven asks in the report

| Asked for | What `netlab` does |
|---|---|
| Connect to the server | Out of scope as a live action; the pod model stands in for a shared session, simulated in memory. |
| Monitor traffic into a pcap | `monitor` replays the simulated frame script; `pcap write` serializes it to a real libpcap file (clearly simulated data). |
| Device/network info for others | `hosts` shows each pod's **fabricated** host table (SSID/IP/MAC/role). No real device is probed. |
| Traceroute | `traceroute` walks the simulated pod topology hop by hop. |
| Send commands to another's terminal (reverse shell) | Not implemented — that is remote control of a victim. `rs-explain` covers the mechanism and the defender's view instead. |
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

## In the browser lab (freestanding) and the non-CLI controls

The portfolio embeds the freestanding browser kernel, not the hosted ELF
shell, so `netlab` also ships as a freestanding analog in
`kernel/freestanding/x86_64/commands.c`. It has no libc and no file I/O, so it
drops the binary pcap file verbs and instead offers `netlab capture <pod>`,
which prints the simulated frames and raises the same ARP-conflict alert
in-line. The other verbs (`pods`, `hosts`, `topology`, `traceroute`,
`monitor`, `arp-demo`, `rs-explain`, `defend`, `consent`) match the hosted
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

`tools/browser-lab/index.html` has a **MitM + RS lab** control panel: a pod
selector, a live/lab status badge, and buttons (Start demo, Hosts, Topology,
ARP-spoof demo, Capture + analyze, Defend, Stop demo). Each button sends the
matching `netlab …` line into the shell, so the whole flow — including the
network switch — runs by clicking, no typing required.

## Related

- `docs/browser-kernel-lab.md` — the freestanding browser kernel and its lab shell.
- `docs/P3_NETWORKING.md` — the real networking stack (sockets, ARP, routing).
- `docs/P3_13_CHAT_SERVER.md` / `docs/SERVER.md` — the real multi-party server.
- The analyzer accepts classic libpcap in either byte order and microsecond or
  nanosecond timestamps; it skips non-Ethernet link types and bounds-checks
  every record, so it is safe to point at arbitrary capture files.
