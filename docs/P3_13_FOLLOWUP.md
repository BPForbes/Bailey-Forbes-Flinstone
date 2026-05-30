# P3-13 follow-up tracker (server foundations from `#239` / PR `#282`)

This file is the single index of all CodeRabbit and Codex review items raised
against PR `#282` and any in-source `TODO` they map to. It exists so the next
reviewer or agent does not have to scroll the PR comment history to confirm
status. The companion roadmap entries live in:

- **`docs/ROADMAP.md`** — central `TODO` callouts table (rows tagged
  `TODO: P3-13 (…)`).
- **`docs/SERVER.md` §4.1.1 / §4.1.2 / §4.1.3** — BSD shim coverage table,
  "no Linux kernel socket required for loopback or TAP" status, and the
  `udpsend` / `udplisten` shell verbs.
- **`docs/P3_NETWORKING.md`** — endian / packet / MLQ framework section,
  architecture ASM table, P3-13 row in the stack vs. product status table.

The contracts already document the wire-level pieces:

- **`contracts/networking/contract_p3_server.h`** — display-name algorithm,
  nick-collision matrix, opcode space, `OP_CTRL_HOST_PROMOTE` payload,
  forward-compat note for `OP_CTRL_HOST_PROMOTE6 (0x24)`.
- **`contracts/networking/contract_p3_session_wire.h`** — frame header,
  opcode reservations.
- **`contracts/networking/contract_p3_sockets.h`** — BSD shim surface.

---

## CodeRabbit `Full Review — P3-13 Server Foundations` — final status

CR ran two rounds against PR `#282`. The first round opened 12 items; the
second round confirmed `7 of 8` of the still-open subset were resolved and
flagged `#12` as needing one extra contract line. That line landed in a later
commit. Current status:

| # | Item | Status | Evidence |
|---|------|--------|----------|
| 1 | `_Static_assert(FL_NET_SERVER_MEMBER_ID_HOST == 1u)` missing | ✅ resolved | `contracts/networking/contract_p3_server.h` (the assert + `FL_NET_SERVER_MEMBER_ID_NONE == 0u` companion live with the struct). |
| 2 | Dead-code `took_into_caller` variable in `fl_net_session_recv_frame` | ✅ resolved | `kernel/core/net/net_server.c` — symbol is no longer in the file. |
| 3 | `*payload_len_out` set to `payload_cap` instead of true wire `plen` | ✅ resolved | `kernel/core/net/net_server.c` — `*payload_len_out = plen` is set before the truncation branch; caller-visible truncation comment documents `*payload_len_out > payload_cap`. |
| 4 | Conservative `n >= MAX_MSG` rejected legal max-length payload | ✅ resolved | `kernel/core/net/net_client.c` — `strnlen(text, MAX_MSG + 1u)` + `n > MAX_MSG` reject; private path subtracts the 4-byte prefix. |
| 5 | `volatile int stop` not strictly C11 atomic | ✅ resolved | `kernel/core/net/server_bg.c` — switched to `atomic_int stop` (`stdatomic.h`). |
| 6 | `parse_endpoint` missing `TODO(#280)` for `[::1]:port` | ✅ resolved | `userland/command/cmd_server.c` line 66 — explicit `TODO(#280)` block above `strrchr`. |
| 7 | `FL_NET_SERVER_EVENT_MSG_PRIVATE` case missing from test event sink | ✅ resolved | `tests/test_p3_server.c` — `last_private[256]`, `case` handler, and `ASSERT(strstr(logJill.last_private, "Hello") != NULL)`. |
| 8 | Contract `OP_*_ANNOUNCE` payload doc said bare display name; impl sends full line | ✅ resolved | `contracts/networking/contract_p3_server.h` — `JOIN/LEAVE/NICK_SET/SERVER_ANNOUNCE` blocks rewritten to the actual wire payloads (`"<display> has joined."`, etc.). |
| 9 | `tests/decode_session_pcap.py` ignored TCP sequence numbers (`-i any` double-count) | ✅ resolved | Decoder sorts segments by `seq`, handles overlap, fills gaps with `\xff` sentinel; opcode counts dropped from 2× to 1× on the rerun. |
| 10 | Flags byte `hdr[3]` not validated | ✅ resolved | `kernel/core/net/net_server.c` — `hdr[3] != 0u` guard in both `fl_net_session_recv_frame` and `_nb` paths returns `FL_RESULT_INVAL`. |
| 11 | `OP_CTRL_HOST_PROMOTE` payload is IPv4-only | 🟡 deferred → **`#283`** | `contracts/networking/contract_p3_server.h` documents the recommended sibling `OP_CTRL_HOST_PROMOTE6 (0x24)` with `[u16 new_id][16 bytes ipv6_be][u16 port]`. Tracked in **`docs/ROADMAP.md`** TODO row **TODO: P3-13 (#283)**. |
| 12 | Nick collision return code: contract said `OP_ERR` only, didn't mention `FL_RESULT_BUSY` for the local C API | ✅ resolved | `contracts/networking/contract_p3_server.h` lines 38–41 *and* `kernel/core/net/net_server.h` lines 95–104 both document the `OK / INVAL / NOENT / BUSY` matrix with explicit cross-reference to the wire-side `OP_ERR`. |

---

## Codex P1 / P2 items — final status

| Severity | Item | Status |
|----------|------|--------|
| P1 | Buffer partial frames before nonblocking polls (TCP frame split across segments) | ✅ resolved — `fl_net_session_rx_t` + `_nb` variant + per-peer parallel `s_member_rx[]` in `kernel/core/net/net_server.c`. |
| P2 | Preserve the initial roster snapshot on join (sync drain dropped `JOIN_ANNOUNCE` + `MEMBER_LIST_SNAPSHOT`) | ✅ resolved — `fl_net_client_dispatch_frame` + sync-drain rewire route the snapshot into the client cache before the BG loop starts. |
| P2 | Restart the client background loop after host closes (stale `g_client_bg` blocked rejoin) | ✅ resolved — `reap_client_bg_if_dead` in `userland/command/cmd_server.c` clears the dead handle before a fresh `server join`. |
| P2 | Author prerelease rows at the entries root (versioning policy) | ➖ N/A in latest passes — `.ver` not touched per maintainer instruction; GH Actions `relocate_root_prerelease_ver_to_preproduction.sh` handles relocation. |

---

## In-source TODOs from the server / packet path

| Location | Tag | What it tracks | Roadmap row |
|----------|-----|----------------|-------------|
| `userland/command/cmd_server.c:66` | `TODO(#280)` | Bracketed-endpoint parsing for `[2001:db8::1]:port` (requires `inet_pton(AF_INET6, …)` and family-tagged `addr_be_out`). | **TODO: P3-13 (#280)** |
| `kernel/core/net/net_background.c:103` | `TODO: P3-14` | Do not recv from `fl_net_netdev_loopback()` in `fl_net_background_tick` — that queue is owned by `net_wire_egress` for ICMP/TCP probes. | **TODO: P3-13 (#238)** |
| `kernel/core/net/net_background.c:107` | `TODO: P3-13` | Wire RX demux calls `fl_net_task_backend_server_ingress()`. | **TODO: P3-13 (#238)** |
| `kernel/core/net/net_background.c:453` | `TODO: P3-13` | Map wire source (port/session) to `from_client_slot`; validate. | **TODO: P3-13 (#238)** |
| `contracts/networking/contract_p3_socket.h:2` | doc note | Full **P3-13** shim TODO — covered by the native non-hosted `fl_socket` path row in the roadmap. | **TODO: P3-13** (native shim) |
| `contracts/networking/contract_p3_wifi_deferred.h:4` | explicit deferral | Wi-Fi 802.11 station mode. | **TODO: P3-13 (#279)** |

---

## Deferred items from the second-round CR sweep (`#282`)

These are items CR raised on the second-round review that are intentionally
deferred to a follow-up PR (with a one-line reason). Everything else from
that sweep landed in the same PR.

| Area | Item | Reason for deferral |
|------|------|---------------------|
| `userland/command/cmd_server.c::promote_thread_main` | Add a session-state mutex around every read/write to `g_client` / `g_client_bg` / `g_server` / `g_server_bg` / `g_server_running`, including from `verb_join` / `verb_leave` / `verb_kill` / `verb_msg` / `verb_connected` / `cmd_server_atexit`. | Correct implementation must also avoid (a) deadlocking against `shell_io_lock` already held by the prompt-aware async output path, (b) holding the session mutex across blocking I/O (`fl_net_session_send_frame`, `fl_net_session_recv_frame*`, `fl_net_sock_accept`), and (c) racing with the bg loops that the mutex itself starts/stops. The current pthread serialization works in practice (verified by the host-transfer tests in `test_p3_server.c`, now augmented with the post-transfer client-side `HOST_PROMOTE` / `HOST_REDIRECT` assertions) because the shell thread is blocked on stdin during the promote window. Splitting the audit into its own PR keeps this review's scope tight; tracked as a fresh follow-up TODO. |

## CodeRabbit recommendation logs (single-device WAN emulation)

PR `#282` discussion thread asked about a `tailscale + tmux + mininet + custom
front-end render` stack for single-device multi-network testing. CR's final
recommendation:

- **CI / Cursor sandbox** → keep the existing `tests/manual_demo_netns_pcap.sh`
  (`ip netns` + Linux `bridge` + `veth`). Cursor sandbox lacks the
  `openvswitch` kernel module, so the raw `netns` recipe is the right fallback.
- **Single dev machine, WAN emulation** → Mininet Python API + `TCLink` /
  `tc netem` (latency, loss, bandwidth caps) + the existing `tmux` panes +
  `tests/render_demo_html.py`. Add when a real Linux dev host with
  `NET_ADMIN` is available.
- **Real multi-network proof** (two physical devices) → Tailscale or manual
  port forwarding — no code changes needed.

Roadmap row: **TODO: P3-13 (single-device WAN demo)**.

---

## How this file stays current

- When an item lands, flip its row from a status emoji (🟡 / ❌) to ✅ and
  point at the commit / file that closed it.
- When CR or Codex raises a new item against P3-13, add a row here *and* a
  matching `TODO: P3-13 (…)` row in `docs/ROADMAP.md`.
- When `#283`, `#280`, `#279`, or `#238` lands, also update the P3 status
  table in `docs/ROADMAP.md` and the P3-13 row in `docs/P3_NETWORKING.md`.
