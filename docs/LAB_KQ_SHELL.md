# KeyQuorum in the browser lab shell

Recorded from source comments removed in PR #416 (2026-11).

The browser lab runs the same KeyQuorum C code as the native shell. This page covers the parts that exist only
in the lab: the stand-ins in `tools/browser-lab/wasm/lab_kq.c`, the page logic in `tools/browser-lab/lab.js` and
`lab-core.js`, and the tests that drive them. The general lab layout is in
[browser-kernel-lab.md](browser-kernel-lab.md).

## What the lab reuses and what it supplies

`lab_kq.c` builds the native checkpoints (`kq_checkpoint.c`), the delivery protocol (`net_kq_delivery.c`), the host
delivery store (`kq_delivery.c`, over the SQLite of the KeyQuorum archive) and the driver (`kq_driver.c`) unchanged.
The file supplies only what a browser has in place of the native shell:

- memory primitives, in place of the assembly layer (WebAssembly has no assembly). `mem_domain_zero` is a volatile
  byte loop. `mem_domain_secure_zero` is that loop followed by a compiler barrier. `asm_mem_ct_equal` reads both
  buffers in full through volatile pointers and accumulates the difference, so it takes the same time for any
  content;
- the host's clock, which is the browser's clock (`fl_time_wall_sec`);
- the working directory (`g_cwd`), which the native shell defines in `common.c`. Relative landing paths resolve
  against it;
- an account directory for the checkpoints, the members table of the lab relay, and the transport that hands frames to
  the page.

Files live under `/kq` (`LAB_KQ_ROOT`), and the host's own directory is `/kq/host`.

## Limits in `lab_kq.c`

| Limit | Value |
|-------|-------|
| lab key slots (`LAB_KEYS_MAX`) | 32 |
| label length (`LAB_LABEL_MAX`) | 32 |
| pending sends (`LAB_PENDING_MAX`) | 8 |
| inbox entries (`LAB_INBOX_MAX`) | 16 |
| how long a send waits for the host (`LAB_SEND_WAIT_SEC`) | 30 s |

The page calls the progress function now and then, and a send that has gone unanswered is reported at that point.

## Keys in the lab

- Each lab account's signing and box keys are made on first use (`local_keys`). The secret stays in the page's
  memory. Nothing secret is seeded: everything in the bundle is public.
- The host keeps the first keys it sees for a name. Different keys for a name already known are refused. The same
  keys are accepted again.
- Names match regardless of ASCII case, as the lab's accounts do. The label kept is the spelling seen first, and
  signatures name that spelling.
- In `lab.js`, the built-in accounts publish no keys. Every visitor's lab seeds them, so their names could belong to
  more than one person. A name held by another member's keys is not published from this page either.

## Commands

`lab_kq.h` declares the commands the server panel and the shell use. Each one runs the same checkpoint as the
native verb:

| Command | Function |
|---------|----------|
| `server file send -t [--permitted] <path> -user <name>` | `fl_lab_kq_send`: encode, then offer and parts |
| `server file permit <path>` | `fl_lab_kq_permit`: the signed-in supervisor countersigns the head |
| `server file log <path>` | `fl_lab_kq_log`: the history of a tracked copy, offline |
| `server file cosign [<delivery>]` | `fl_lab_kq_cosign`: list requests, or co-sign one |
| `server file open <delivery>` | `fl_lab_kq_open`: decode a released letter |

A recipient counts a permit only when its signer is the file's scope owner or an ancestor of it. The lab's accounts
are flat names, so the owner permits its own head there. A separate supervisor is covered by the native checkpoint
tests.

## Lab page behaviour (`lab.js`, `lab-core.js`)

- **Isolation.** While the lab is isolated, the real relay stays down and no KeyQuorum frame leaves the page. A file
  send in that state is refused with a line saying so. Reconnecting is not allowed while isolated, because it would
  bypass the boundary the guest enforces.
- **Network mode.** The guest prints `LAB_NETMODE lab` or `LAB_NETMODE live`, and the page drops or restores the real
  network to match. A booted or reset guest starts live, and the isolation flag is cleared with the guest.
- **Netlab panel.** It is shown only when the build's manifest includes netlab. Otherwise its buttons would answer
  "unknown command".
- **Accepting files.** `server file accept` is the only answer that moves waiting files. `lab-core.js` returns every
  waiting file in order, or a line saying there are none. It answers for the session's own user only; other users'
  files keep waiting.
- **Terminal.** Text is 16 VGA slots mapped to 24-bit sRGB, so the terminal can use truecolour while the guest writes
  4-bit attributes. The guest's bytes go through a streaming `TextDecoder`, so a UTF-8 sequence split across reads is
  not turned into mojibake. The 80 by 25 text buffer is rendered from physical `0xb8000`. This is a text-mode display,
  not a graphics-mode VGA.

## Entry point

`wasm_main.c` is the WebAssembly entry. It starts the identity, the RAM file system, the lab disk, the lab network and
the interactive shell, and it calls `fl_lab_kq_bind_identity` to bind the lab's accounts. The file's own note says
`kernel.c` is not compiled here, because it needs the interrupt table and `hlt`.

## Tests

- `scripts/test_browser_kq.cjs` (Playwright, needs the lab built with KeyQuorum: `EMCC_REQUIRED=1 make wasm`): a
  co-signed file and a plain file between two lab pages through the server panel and the local relay hub.
- `tests/test_lab_kq_wasm.js`: the same delivery in two copies of the module, joined through the relay room with the
  lab's wire encoding. It skips when the module was built without KeyQuorum. Its assertions include that the relay
  never carried the plaintext, that a second revision needs a permit, and that only the recipient can co-sign.
- `tests/test_browser_lab.js`: a guest's `SERVER_FILE` line becomes a file event with its bytes. Received files are
  typed into the guest in parts that fit its 128-character line. Waiting files are typed only in answer to
  `server file accept`.
