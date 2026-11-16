# Signed command log (FL-04)

The command log records what the shell was asked to run, in a separate, versioned, append-only
journal. Each record is signed and carries the hash of the record before it, so a reader can
check that nothing was changed, removed from the middle or reordered. It is not the per-file
`.kqtf` history and it does not write raw disk sectors; a design review recommended this shape.

Code: `userland/shell/cmd_journal.{c,h}`. Test: `make test_cmd_journal`
(`tests/test_cmd_journal.c`). Hosted builds only. The interpreter hook is **not wired in yet**.

## What a record holds

One record per event, big-endian, length-delimited, built and read with the shared byte codec
`kernel/core/net/net_codec.h` (the header comment in `cmd_journal.h` lists every field and its
size). Strings carry a one-byte length and may not contain a NUL:

- format version (`1`), the signing-domain string `flinstone.cmdlog.v1`, a journal id and a
  session/device id;
- a sequence number (0, 1, 2, ... with no gap, across segments) and the SHA-256 of the whole
  previous record (zeros for the first);
- an event id, the signer's key id (one key per journal) and the account label the command ran as;
- the event type: `ATTEMPT` (before the command runs), `COMPLETION` (with the command's result
  code) or `REFUSAL` (refused after its attempt, with a code);
- a **redacted command category**, never the arguments;
- a wall-clock timestamp and a trust flag (always "local clock" for now);
- a correlation id: an ATTEMPT's own event id, which its COMPLETION or REFUSAL repeats;
- room for a sudo co-signature (nonce, second signer id, second signature), unused for now;
- the signature, over every field after the length prefix.

### Redaction

The category is the first word of the line when it names a known shell command (the command
registry `fl_shell_cmd_lookup`, plus the line-mode builtins such as `exit` and `history`), and
`other` for anything else. Nothing after the first word is copied; the scratch copy of the line's
start is wiped. So `passwd <secret>` is recorded as `passwd`, `sudo -S <secret> ...` as `sudo`,
and a secret typed as the first word as `other`. An allowlist for future per-command fields
exists and is empty.

## Storage

The journal writes through `fl_kq_storage_t` (`kernel/core/vfs/kq_storage.h`), the same table the
KeyQuorum checkpoints use (see `docs/KQ_STORAGE.md`); the shell would pass `fl_kq_cp_storage()`.
Segments are `<dir>/cmdlog-NNNNNN.seg`. The table has no append, rename or remove call, so each
append rewrites the active segment whole with `write_atomic` (bounded segment replacement): on the
placement layer that is a staged write, an fsync of the staged file, and a rename over the old
file.

Bounds: a record (1 KiB), a segment (64 KiB), the total (4 MiB) and retention (64 segments) by
default; a config may lower them. Segments are never pruned: pruning needs a remove call and a
signed anchor so the chain can start mid-way. When a bound would be passed, the write fails and
the failure policy applies.

### One writer per journal

Writers through one handle are serialized by its mutex. A second `fl_cmdlog_open` of the same
storage table and directory in the same process is refused (`FL_CMDLOG_OPEN_BUSY`), because two
handles would each rewrite the segment and the last writer would silently drop the other's
records. **There is no cross-process lock**: the storage table has no lock call. Each process or
session must use its own journal directory. Close a handle only after every caller is done with it.

## Verify and recover

`fl_cmdlog_verify` walks the segments in order and checks the format and domain, each signature,
the sequence and the hash chain. A journal is bound to **one device id and one signer key id**:
every record must carry the verifying signer's key id and the journal's device id (and, when the
caller gives one, the expected device id), so a record signed with any other key the verifier
knows is rejected. It reports:

- a tampered record (bad signature), a reordered or repeated record (bad sequence), a record whose
  previous-record hash does not match (broken chain), a record from another journal, device or key
  (foreign), or an unreadable segment;
- a **gap**: a segment exists after a missing one, after a partial record, or past the retention
  bound. Segment indices are probed up to a hard bound (1024), so a missing middle segment can
  never make the rest of the journal invisible;
- an **incomplete tail**: the very last segment ends inside a record (a torn write);
- every ATTEMPT with no COMPLETION or REFUSAL: its **outcome is unknown**.

`fl_cmdlog_open` runs the same walk, against its own device id and key, and continues the chain. A
journal that does not verify, or belongs to another device or key, is not opened, because
appending would hide the evidence; nothing is ever written over a segment the handle did not
create. A torn tail is reported and dropped from the active segment by the next append, and a
write that fails part-way is treated the same way, so a partial record never ends up in the
middle of the journal.

### Lockout and operator recovery

Under the refuse policy two states refuse every command once the interpreter hook is wired in,
the commands that would repair things included:

1. **The journal does not verify** (tampering, a gap, a foreign record, an unreadable segment).
   `fl_cmdlog_open` fails, so the shell has no journal and every attempt is refused.
2. **The journal is full.** Retention is fail-closed: at the defaults (4 MiB, about 400 bytes per
   command for its two records, depending on label lengths) that is roughly 10,000 commands.

Recovery is an operator step outside the shell: move the journal directory aside (keep it, it is
the evidence), check it with `fl_cmdlog_verify`, and let the shell start a new journal in an empty
directory. The new journal does not yet point back at the old one. Before the hook lands, a
rotation should start the new journal with a signed seq-0 anchor that carries the old journal id
and last hash, which needs no remove call.

## Failure policy

**Owner decision (Nov 16): refuse with a red error giving the reason.** When the ATTEMPT record
cannot be written (storage failure, a bound reached, the signer refused, or no journal open),
`fl_cmdlog_attempt` returns `FL_CMDLOG_REFUSE` with a short reason, and the command does not run.
The caller prints it with `fl_cmdlog_print_refusal`, which uses the shell's error colour
(`fl_color_error`):

    [ERROR] command refused: the signed command log could not be written (<reason>)

A COMPLETION that cannot be written cannot undo a command that already ran: `fl_cmdlog_complete`
returns `-1` and a reason, the caller warns with `fl_cmdlog_print_completion_gap`, and recovery
reports that attempt as outcome unknown.

## Signing

The journal signs through an interface (`fl_cmdlog_signer_t`: `sign`, `verify`, `key_id`), not a
hard dependency. **Missing:** KeyQuorum has no generic, domain-separated single-signer C API yet.
It needs a call that signs and verifies `(domain, message)` with an account's key in
`keyquorum_identity.h`, and two matching ops in the driver wrapper
`kernel/drivers/keyquorum/kq_driver.c`; then a signer over them replaces the test-only HMAC signer
in `tests/test_cmd_journal.c`. The sudo co-signature fields wait on the same work
(`sudo_sign` / `sudo_verify` exist in the driver table for elevation, not for this log).

## What it does not guarantee yet

- **Rollback without a witness.** Cutting whole records off the end, or replacing the journal with
  an older copy, verifies cleanly: nothing outside the device remembers the last sequence number
  and hash. A witness (a relay, a peer or a monotonic counter) is needed.
- **Power-loss durability.** The rewrite-and-rename is crash-consistent on the placement layer, but
  no backend in the tree fsyncs the directory after the rename (`write_atomic_at` in
  `server_shared_fs.c` fsyncs only the staged file, and the facade calls the same function), so a
  power cut can lose the rename. None of this has been tested against a real power cut.
- **Several processes on one journal.** Only a second open in the same process is refused.
- **Commands that bypass the interpreter**, such as background jobs, network-triggered actions or
  anything run outside `execute_command_str`, are not logged; nor is anything until the hook is
  wired into the interpreter.
- **The freestanding shell** (`kernel/freestanding/`) has no command log.
- A MAC-based test signer proves nothing about authorship; only a KeyQuorum signature will.
