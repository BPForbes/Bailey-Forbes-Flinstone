# Signed command log (FL-04)

The command log records what the shell was asked to run, in a separate, versioned, append-only
journal. Each record is signed and carries the hash of the record before it, so a reader can
check that nothing was changed, removed from the middle or reordered. It is not the per-file
`.kqtf` history and it does not write raw disk sectors; a design review recommended this shape.

Code: `userland/shell/cmd_journal.{c,h}`. Test: `make test_cmd_journal`
(`tests/test_cmd_journal.c`). Hosted builds only. The interpreter hook is **not wired in yet**.

## What a record holds

One record per event, little-endian, length-delimited (the header comment in `cmd_journal.h`
lists every field and its size):

- format version (`1`), the signing-domain string `flinstone.cmdlog.v1`, a journal id and a
  session/device id;
- a sequence number (0, 1, 2, ... with no gap, across segments) and the SHA-256 of the whole
  previous record (zeros for the first);
- an event id, the signer's key id and the account label the command ran as;
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
placement layer that is a staged write and a rename over the old file.

Bounds: a record (1 KiB), a segment (64 KiB), the total (4 MiB) and retention (64 segments) by
default; a config may lower them. Segments are never pruned: pruning needs a remove call and a
signed anchor so the chain can start mid-way. When a bound would be passed, the write fails and
the failure policy applies. Writers are serialized per journal by a mutex.

## Verify and recover

`fl_cmdlog_verify` walks the segments in order and checks the format and domain, each signature,
the sequence and the hash chain. It reports:

- a tampered record (bad signature), a reordered or missing-in-the-middle record (bad sequence or
  broken chain), a record from another journal, or an unreadable segment;
- an **incomplete tail**: the last segment ends inside a record (a torn write);
- every ATTEMPT with no COMPLETION or REFUSAL: its **outcome is unknown**.

`fl_cmdlog_open` runs the same walk and continues the chain. A journal that does not verify is not
opened, because appending would hide the evidence. A torn tail is reported and dropped from the
active segment by the next append.

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
  not every backend fsyncs the directory, and none of this has been tested against a real power
  cut.
- **Commands that bypass the interpreter**, such as background jobs, network-triggered actions or
  anything run outside `execute_command_str`, are not logged; nor is anything until the hook is
  wired into the interpreter.
- **The freestanding shell** (`kernel/freestanding/`) has no command log.
- A MAC-based test signer proves nothing about authorship; only a KeyQuorum signature will.
