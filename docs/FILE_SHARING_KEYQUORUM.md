# Server file sharing on KeyQuorum's file model

Status: **design only**. Nothing in this document is implemented. It maps KeyQuorum's
file-sharing features onto Flinstone's server file sharing (`server file`, the TCP hub in
`net_server.c` and `net_file_delivery.c`, and the server store in `server_shared_fs.c`) and
states what each one does for confidentiality, integrity and availability (CIA). The
heritage and bridge rules it depends on are in [IDENTITY_HERITAGE.md](IDENTITY_HERITAGE.md).

## What exists today

| Piece | Flinstone now | Gap |
| --- | --- | --- |
| Permissions | 16-bit file perms (`contract_p5_file_perms.h`): VIEW, WRITE, EDIT, RUN, OVERWRITE, DOWNLOAD, SERVER_SHARE, ACCEPT, plus LOCKED, EXPIRES, PUBLIC, REVOKED. `server file` takes `-v -w -e -r -o` and `--expire` | No per-recipient cryptographic check; the hub decides |
| Transport | Plain TCP hub, file bytes up to 16 MiB (`FL_SERVER_FILE_MAX_BYTES`) | Nothing sealed in transit to the recipient beyond the channel |
| Store | `server_shared_fs` writes files and `.meta` sidecars; `server_shared_db` holds rows | Payload is plaintext on disk |
| History | `audit_log.c` lines (`type=shell`, `authz`, `identity`) with `actor=` | No per-file, hash-chained history; no integrity over the file's life |
| Expiry | `server_file_expire.c`, deletes when the time passes | Copies already delivered are not recalled |

KeyQuorum already has all of the cryptographic and history pieces, in Rust:

- **Tracked files** (`src/file_history/`, `KQTF`): a container of revisions, each with a
  payload commitment, a hash-chained event history, signatures, and proofs. Trust is decided
  by `FilePolicy` (`scope_owner`, `descendants`, `ancestors`, `cross_branch`, `auto_merge`;
  `standard()` defaults), never by the container.
- **Sealed letters** (`src/file_delivery.rs`, `src/envelope.rs`): file delivery
  (`KIND_FILE_DELIVERY` 13, acknowledged by `KIND_FILE_DELIVERY_ACK` 14), tracked-file history
  (`KIND_FILE_HISTORY` 15 and its ack 16), snapshot (17), request and answer (18, 19). Every letter is sealed to the recipient's
  X25519 key and signed by the sender; the answer is signed back.
- **Transport**: outbox and inbox rings (`src/outbox.rs`, `src/ring.rs`) over a relay mailbox
  (`src/relay/`) that stores opaque letters and never unseals them.
- **Gates**: quorum (`quorum.rs`) and password (`locked_files.rs`) gates, `file expire`
  (tombstone), finalization, countersignature, bridge approval, merge, resolve, verify.

## Confidentiality: what is and is not encrypted (checked in KeyQuorum)

- **In transit between accounts: encrypted.** Every letter goes through `envelope::seal` to
  the recipient's registered encryption key. The relay holds only that opaque envelope.
- **At rest in a `.kqtf` container: not encrypted.** `src/file_history/container.rs` stores
  revision payloads as plain bytes. Encryption exists only where a gate is linked
  (`file link --quorum-file` or `--locked-file`), and then the gate's ciphertext holds the
  file, not the container.
- **Implication for Flinstone.** A tracked file on the server is readable to anyone who can
  read the server's disk, exactly as a plain Flinstone file is today. Confidentiality at rest
  therefore needs a choice (see Decisions, D1). It is not free just by adopting KeyQuorum.

## The mapping

### Who may do what

KeyQuorum's `FilePolicy` already encodes the authority rule Flinstone wants: authors sign,
the scope owner or an ancestor may decide, and another branch needs a bridge. Map it as:

| Flinstone action | KeyQuorum rule | Notes |
| --- | --- | --- |
| Create a file on the server | `track` with the scope at the creator's key id (`Root.Flinstone` etc.) | The creator's label is the scope owner by default |
| VIEW / DOWNLOAD a file | Recipient is the scope owner, a descendant, or an ancestor; or an accepted file request (`KIND_FILE_REQUEST`) | Descendants see what their ancestors share down |
| WRITE / EDIT (a new revision) | Author signs (`AuthorSign`); the policy judges it | A revision is trusted only if the policy says so |
| OVERWRITE | Not a new revision: refuse. History is append-only | Overwrite stays a Flinstone-only flag for the store's own files |
| Share across branches | Bridge accepted (`bridge accept`), then `file share`; `AuthorSignBridgeOrOwner` for cross-branch edits | Bridges come from `kernel/core/identity/bridge.c`; the signed bridge approval is `file bridge-approve` |
| ACCEPT (take a file) | `file receive` with the recipient's own accept or reject, signed | Idempotent by delivery id |
| EXPIRES | `file expire` (scope owner or ancestor) and `EXPIRED_ACCESS_ATTEMPT` on later touches | Tombstone keeps history and proofs |
| REVOKED | No KeyQuorum primitive for a single share; use `file expire`, or stop the bridge | Recall is not possible for copies already delivered |
| LOCKED | A quorum or password gate linked to the tracked file | Only way to encrypt payloads at rest today |
| PUBLIC | Not mapped: no anonymous reader in KeyQuorum | Keep Flinstone-only |
| RUN | Not mapped: KeyQuorum files are data, never executed | Keep Flinstone-only; never run a letter's content |
| SERVER_SHARE | Not mapped as a right; it is a Flinstone relay role | Keep Flinstone-only, but a server share still produces a letter |

### Integrity

- Each revision is signed by its author; `file status` derives the trusted revision from the
  store's keys, not from the file. A receiver sees `FIRST`, `REPLAYED`, `NEWER` or
  `NOT_NEWER` (`file_delivery::freshness`), so a replayed old revision is visible.
- The history is a hash chain, optionally with event signatures (`EventProof`); `file verify`
  and `file open-history` check it against a snapshot (`KQHS`), giving a point-in-time
  comparison (`SAME`, `LOCAL_AHEAD`, `REMOTE_AHEAD`, `DIVERGED`).
- Merges and conflicts (`file merge`, `file resolve`, `file_history/merge.rs`,
  `resolve.rs`) stop for a human on overlapping edits; Flinstone should keep that rule and not
  add its own.

### Availability

- The hub is one point of failure today. The KeyQuorum transport is store-and-forward: a
  letter waits in the relay until it is pulled, and the outbox ring (32 by default) keeps
  unsent letters rather than dropping them. A full ring refuses rather than overwrite.
- Relay letters expire on the relay's own TTL (device letters: `DEVICE_PACKAGE_TTL_DAYS`).
  Delivery must therefore be pulled within that window; the sender sees the outbox state.
- Backups: a server store holding tracked files needs the same sealed-backup thinking as the
  KeyQuorum relay (`docs/operator/r2-backups.md`), or the store's own copy is the only copy.
- Denial of service: the 16 MiB hub limit and the ring capacities bound memory; the hub must
  keep both.

### Audit and history

- Flinstone keeps `audit_log.c` for shell and authorization events. KeyQuorum's history
  (`FILE_*`, `SHARE_*`, `POLICY_DECISION`, `CONFLICT_*`) is per file and hash-chained.
- Rule from KeyQuorum: history never records secrets, and every detail key is in
  `event::SAFE_DETAIL_KEYS`. Flinstone must not add a detail that carries a password, a
  passphrase or a payload.
- Flinstone's `actor=` (heritage key id) and KeyQuorum's event actor should be the same label,
  so one `Root.Flinstone` means one identity in both logs.

## Integration options

1. **Process boundary.** The shell runs `keyquorum file ...`, `send`, `inbox` as child
   processes. Smallest change; no new C ABI; the rules stay in KeyQuorum; each command pays
   process and file I/O cost. Good for a first working version and for auditing.
2. **C ABI for file history.** Extend `ffi/identity` (or a sibling crate) with a few
   functions: check in a revision, verify a container, build a letter, open a letter. The
   crate calls the same KeyQuorum modules. Preferred for the server hot path, because the hub
   answers many requests.
3. **Full Rust library.** Port the hub to Rust. Not recommended now: it moves the whole
   network layer and most of the kernel's policy.

**Recommendation:** start with (1) to prove the flows end to end on the lab-style test
harness, then move the hot path to (2). Each step keeps a single copy of every rule in
KeyQuorum, which is what the heritage work already relies on.

## Key custody

Each account needs a KeyQuorum encryption key (to receive letters) and a signing key (to sign
revisions and answers). These are per account, derived from the key id, and never shared
between accounts. Custody options:

- A passphrase-wrapped slot per account (`keyquorum-device`), prompted once per command, held
  in memory and zeroized when the command ends. Simple; the passphrase is the weak point.
- A hardware key per account (one key, one device). Stronger; needs a device per account.
- Root's key held offline; it is the master, so a root action needs the root slot.

Bridges are then signed by both sides' keys, not just recorded in SQLite.

## Decisions for the owner

- **D1 Encryption at rest.** Options: (a) encrypt server store payloads with a per-file key
  wrapped to the scope owner (a sealed file), (b) require a quorum or password gate for any
  file that is not public, (c) accept plaintext at rest and document it. Recommendation: (a)
  for every tracked file, (b) optional for the most sensitive ones.
- **D2 Recall.** Once a copy is delivered, KeyQuorum cannot recall it. Expiry and revocation
  only stop the server's copy and future access. Decide whether that is acceptable, and say so
  in the operator docs.
- **D3 Flinstone-only rights.** Confirm that PUBLIC, RUN and SERVER_SHARE stay in Flinstone's
  own perms, and that RUN never executes content from a letter.
- **D4 Transport.** The Flinstone hub carrying sealed letters, or a KeyQuorum relay for
  cross-machine delivery. Recommendation: the hub carries letters for one machine's accounts;
  a relay is needed only when accounts sit on different machines.
- **D5 Integration mode.** Process boundary first, then the C ABI (see above).

## Build order (superseded: see Plan above)

1. Key custody: one encryption and signing key per account, created by `useradd`, stored in
   the account store as sealed slots. No file change yet.
2. Process-boundary prototype: `server file` creates a tracked file and shares it to a
   descendant or a bridged account, through `keyquorum file` and `inbox`.
3. Trust and history: map `FILE_*` events into the server's view; `file status` and
   `file verify` exposed in the shell.
4. Encryption at rest (D1) and gates for LOCKED.
5. Hot path over the C ABI; backups; a restore drill before any claim of availability.

Each step needs its own `.ver` change under Flinstone's versioning rules.

## Wire format for file bytes

Decided: **big-endian** (network byte order), for every integer in a file's bytes, whether it
travels in a letter, a container or a server store.

Why big-endian: KeyQuorum's codec already writes every length and integer big-endian
(`envelope::push_len_prefixed` and `push_len_prefixed_u32` use `to_be_bytes`, and the reads use
`from_be_bytes`), and the tracked-file codec reuses those helpers. Flinstone's IP, UDP and TCP
framing is big-endian too (`net_endian.h`). The one little-endian path in Flinstone is IEEE
802.11 management frames, which carry no file bytes. A second order would need its own helpers
on both sides and buy nothing.

Layout rules:
- **Linear, no padding, no pointers.** Fields follow one another in a fixed order.
- **Every variable field is length-prefixed.** Use a `u16` length for names and labels (at
  most 255 characters in practice), and a `u32` length for payloads. A `u16` cannot hold a
  16 MiB payload, so payloads never use it.
- **A version field comes first** in every container and letter, so a parser can refuse what
  it does not know before it reads further.
- **Every read is bounds-checked** against the bytes remaining, and an overrun is an error,
  never a silent truncation. A declared length above the remaining bytes or above the limit
  for that field is refused.
- **Codes are appended, never renumbered**, as the existing codecs already require.

These rules are the existing KeyQuorum codec's rules, so nothing new is invented. Flinstone
code that reads or writes these bytes uses the same big-endian helpers the rest of the shell
uses, and does not add a second copy.

## Plan

This plan turns the requirements below into phases. Each phase has an owner repository, an
exit test and the documents it changes. Phases run in order unless noted. Nothing here is
built yet.

### Scope

In scope: per-account KeyQuorum keys, deletion with key redistribution, signed and time-bound
access to edited files, a server-side edit log, host-countersigned public offers, and
background KeyQuorum processing for every file operation on the server.

Out of scope for this plan: encryption at rest (D1) and recall of delivered copies (D2),
which need owner decisions first; a second operator role; a hardware-key custody option;
the browser lab and the freestanding kernel.

### Phase 0: decisions

- Settle D1 (encryption at rest) and D2 (recall).
- Exit: both recorded in this document with the chosen option.
- Blocks: phases 2, 4 and 6 (the encrypted-at-rest variant changes their design).

### Phase 1: KeyQuorum authority and history rules (KeyQuorum repo)

- Add the redistribution authority rule to `authority.rs`: a parent may grant a deleted
  child's reachable files to itself and its direct children, and no one else.
- Add the time-bound query to `file_history`. Checked: no such query exists. The nearest,
  `policy::latest_trusted_revision`, returns the last *stored* trusted revision, not one chosen
  by time. The time the query needs must come from the server, since a revision's
  `created_at_utc` is the editor's own clock (signed, but not checked by the server). Decide
  which clock governs access (see the Phase 1 notes below).
- Use `finalize` (not `countersign`) as the host's signature for PUBLIC offers. Checked:
  `policy::is_finalized` accepts a finalization only from the scope root or an ancestor of it,
  and only on a revision that is already trusted. `countersign` is a supervisor approval that
  the file's requirement rule (`AuthorSignDirectParent`, `AuthorSignBridgeOrOwner`) decides,
  not a host-only signature.

Phase 1 notes from checking the code:
- The access time must be the server's time of acceptance, recorded per revision outside the
  container (as `tracked_seen_roots` is). Otherwise the server would trust the editor's clock.
  The per-revision record is new; I have not checked whether `tracked_seen_roots` holds a time.
- Trust is judged per revision, but a fork has two heads and no single current revision
  (`policy::current_revision` returns nothing). The time-bound rule must say which branch
  it answers for, or return the newest trusted revision on each branch.
- `finalize` means "this is the revision to rely on", which is close to but not the same as
  "public". Either reuse it and document that a PUBLIC offer is a Root finalization, or add a
  new proof kind for publication. Proof-kind codes are wire format: a new kind is appended,
  never renumbered.
- The PUBLIC flag is a Flinstone permission bit with no KeyQuorum counterpart, so the offer
  check must be written in Flinstone: a PUBLIC offer names a revision and requires a Root
  finalization proof on it.
- Exit: unit tests in each module's `tests.rs` pin the rules (the Paul, Ashly and George case,
  an unsigned later edit, an edit after the cut-off time, a public offer without the host
  signature). `cargo fmt`, `clippy -D warnings` and `cargo test` pass, and the `ffi/identity`
  checks pass.
- Documents: KeyQuorum's `docs/` file-sharing notes and `AGENTS.md`/`CLAUDE.md` if the rules
  change.

### Phase 2: C ABI for file history (KeyQuorum repo, `ffi/identity`)

- Export calls for: check in a revision, verify trust as of a time, build and open a letter,
  read the edit log, and the redistribution grant.
- Each call returns the same codes as the heritage calls and writes no secret to an output
  buffer that the caller did not size.
- Exit: a C header entry for each call, Rust tests through the C ABI, and a Flinstone build
  that links the new library.
- Documents: `ffi/identity/include/keyquorum_identity.h` and the Flinstone build notes.

### Phase 3: per-account keys (Flinstone repo)

- `useradd` generates the account's encryption and signing keys through the ABI, stores them
  as sealed slots in the account store, and records their public halves.
- Root's keys are created at first setup and held under the same rules.
- Exit: a test creates an account, reopens the store, and signs and verifies a revision.
- Documents: `docs/IDENTITY_HERITAGE.md` (storage and migration sections).

### Phase 4: deletion and redistribution (Flinstone repo)

- `userdel` runs the redistribution (the grants from phase 1), logs each grant, deletes the
  account's tokens, and removes the account, as one unit.
- If any step fails, nothing is left half-moved: the account and its tokens stay, and the
  error says which step failed.
- Exit: the Paul example passes end to end. Ashly and George can open `Plan.txt` and
  `Presentation.pptx`. Paul's key id cannot be used afterwards. A forced failure in the middle
  leaves the account intact.
- Documents: `docs/IDENTITY_HERITAGE.md` (authority table and `userdel` row).

### Phase 5: background processing on file operations (Flinstone repo)

- The hub and the `server file` commands call KeyQuorum automatically on share, receive,
  edit, open and offer. No `keyquorum` command is run by the user.
- Each refusal shows its plain reason (see requirement 6) and records the raw error in the
  audit log.
- Exit: each refusal message has a test. A user can share and open a file with no KeyQuorum
  command typed.
- Documents: `docs/FILE_SHARING_KEYQUORUM.md` (this file) and the shell help text.

### Phase 6: signed, chained edit log (Flinstone repo)

- Each edit is signed by its editor and appended to a hash-chained server log that names the
  editor, the time and the revision. The log is not in the plain-line `audit_log.c` format.
- The log view is available to anyone allowed to read the file.
- Exit: a tampered entry fails verification. Access returns the newest revision signed at or
  before the request time, and a later unsigned edit is never returned.
- Blocks on: D1 if payloads are sealed at rest.

### Phase 7: public offers (Flinstone repo)

- A PUBLIC offer is accepted only with the host countersignature on the revision. Without it
  the offer is refused with "public offer lacks the host signature".
- Exit: a public offer with and without the signature is tested.

### Phase 8: availability

- Back up the account store and the server's key material. Write a restore procedure.
- Run a restore drill and record the result before any availability claim.
- Exit: the drill passes on a clean machine, and the result is recorded here.

### Phase 9: verification and documents

- Test matrix: each acceptance case above, plus the failure-path tests from phases 4 to 7.
- `docs/soc2-controls.md`-style mapping for any control the work adds, in both repositories.
- Versioning: one `.ver` change per PR under the Flinstone rules; PRERELEASE and DEV_VERSION
  as they stand.
- Submodule: move the Flinstone pin from the feature-branch commit to the merged KeyQuorum
  `main` commit.
- Exit: CI green on both repositories, and the documents above updated.

### Acceptance cases

1. Paul's deletion grants `Plan.txt` and `Presentation.pptx` to Alternative, Ashly and George,
   and Paul's key id no longer works.
2. A requester reading a file gets the newest revision the editor signed at or before the
   request time. A later unsigned edit is refused with "revision unsigned by its author".
3. Every edit appears in the chained log with its editor and time, and a tampered entry fails.
4. A PUBLIC offer without the host countersignature is refused.
5. A failure during `userdel` leaves the account, its tokens and its files unchanged.
6. A user shares and opens a file without typing any KeyQuorum command.

### Risks

- **Redistribution widens access.** Siblings gain files they could not reach before. This is
  the stated policy; it needs the audit entry to be reviewable.
- **Growing key store.** Per-account keys add sealed slots to the store that backups must cover.
- **Time-bound access depends on clocks.** Times come from the server; a wrong server clock
  changes which revision is returned. Record the server's time source in the log.
- **Scope.** The ABI, the redistribution rule and the chained log are each a sizeable change.
  Keep each phase in its own PR.

## Owner requirements (decided)

These replace the open items they answer. Where a requirement conflicts with a rule above, the
requirement wins.

1. **Keys belong to the account.** Each account holds its own KeyQuorum tokens (its encryption
   and signing keys). Deleting an account deletes its tokens.
2. **Keys are redistributed by the parent when an account is deleted.** Access the deleted
   account had moves to its parent and its siblings, so their files stay reachable. Example:
   `Root.Alternative` holds `Ashly`, `George` and `Paul`. Deleting `Root.Alternative.Paul`
   removes that key id and its tokens. `Plan.txt` and `Presentation.pptx`, which Paul could
   reach, are then granted to `Root.Alternative` and to `Ashly` and `George`, so the two siblings
   can open them. Nothing is granted to another branch.
   - The redistribution is an explicit, logged act of the parent, not a silent copy. It
     records the deleted key id, each file, and each grantee, in the server's audit trail.
   - It must not revive the deleted key: its tokens are gone and no new token is minted under
     its key id.
   - If file payloads are later sealed at rest (D1), this step re-wraps each file's content key
     to the grantees. The parent can do that only for files it can reach, so the re-wrap is
     limited to those.
3. **Edits are signed and time-bound, git-style.** A requester cannot read a revision unless the
   editor's signature covers it. Access resolves to the newest revision the editor signed at or
   before the time the requester asks for, never a later unsigned edit. This is KeyQuorum's
   trusted-revision rule (`file status`, `file_delivery::freshness`), applied at access time.
   - Every edit, who made it, and the signed result are logged on the server as a hash-chained
     history, and a `file log`-style view is available to anyone allowed to read the file.
4. **Public files need the host's signature.** A file offered with PUBLIC is trusted only when
   the host (Root, or the scope owner's ancestor it names) countersigns the revision. Without
   that signature the offer is refused.
5. **KeyQuorum runs in the background as a processor.** The Flinstone module that moves data
   calls KeyQuorum automatically when a file is shared, received, edited, opened or offered.
   The user never runs a `keyquorum` command. Consequence: the C ABI (integration option 2)
   is required, not optional, so D5 is decided in its favour.
6. **Failures are surfaced, not hidden.** If a signature, proof, freshness check or history
   entry is missing or invalid, the operation fails with a plain reason (for example "revision
   unsigned by its author", "history does not match the server's copy", "public offer lacks the
   host signature"). The raw KeyQuorum error and the event are recorded in the audit log.

7. **Access time is the server's acceptance time.** A request "as of T" returns the newest
   trusted revision the server accepted at or before T. The editor's `created_at_utc` is kept
   as the signed claim but does not decide access. Forks are answered per branch.
8. **Public offers are host finalizations.** A PUBLIC offer names a revision and needs a
   finalization proof from Root (the scope root or an ancestor of it) on that revision, which
   must already be trusted. No new proof kind is added for now.
9. **The host governs, but does not dictate.** The host (Root) keeps the governance it needs to
   run the server: it signs public offers (item 8), keeps the audit trail, and sets the server's
   own limits (size, rate, retention). It does not decide who may read or edit a file beneath
   it. Those decisions follow the file's policy and the authority tree (item 2 and the
   `FilePolicy` rules). The host's own role therefore has no override over file-level trust,
   and the server's acts (grants, public offers, deletions) are logged with the host's key id.

10. **The host is a blind medium.** The host carries data from A to B and cannot read it. The
    only content the host may read is a document a person deliberately sends to the host
    (for example a PUBLIC offer). This decides D1: payloads are encrypted, and the host holds
    ciphertext only.

Consequences of item 10 (to design in Phase 0 and Phase 6):
- **Per-file content keys.** Each tracked file gets a content key, wrapped separately to each
  account allowed to read it: the scope owner, its ancestors, and any grantee. The host holds
  wrapped keys it cannot open.
- **Redistribution re-wraps, not copies.** On deletion (item 2), the parent unwraps the file
  key with its own key and wraps it for Ashly and George. The parent can do this only for files
  it can reach, which the authority rule already requires.
- **Storage format.** The `.kqtf` container currently stores payloads as plain bytes
  (`src/file_history/container.rs`). It must store ciphertext for non-public files. That is a
  container format change, so it needs a version bump and a migration, and new fields are
  appended, never renumbered.
- **Revision ids.** A revision id is derived from the payload, which the host cannot read.
  Either the signature covers a keyed commitment (the pattern history letters already use), or
  the id is computed over the ciphertext. Decide before Phase 1 is coded.
- **Server-side checks are metadata-only.** The host can check signatures over revision ids,
  the event chain, freshness and the time rule (items 7 and 8). It cannot check the content
  itself. Recipients check content on receipt.
- **Public files are the exception.** A PUBLIC file the owner sends to the host is readable by
  the host by design, and anyone may read it. Its payload may be stored in clear.
- **Lost keys.** If an account's keys are destroyed (item 1) and no ancestor holds the file
  key, the file is unrecoverable. Redistribution (item 2) is the only protection, so the
  wrapping rule must always include an ancestor.

Still open: D2 (recall). Delivered copies cannot be recalled, and nothing in these requirements
changes that.

## Not yet covered

- No code, no ABI and no keys exist for any of this. The heritage key ids are labels only.
- The browser lab and the freestanding kernel do not share this model.
- The 16 MiB limit, the ring sizes and the relay TTL are KeyQuorum defaults, not measured for
  Flinstone's workload.
