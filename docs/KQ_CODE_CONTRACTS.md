# KeyQuorum code contracts (host store, checkpoints, monitor)

Recorded from source comments removed in PR #416 (2026-11).

These are the rules a maintainer needs when changing the KeyQuorum C side in `kernel/core/vfs/`. The
delivery story is in [FILE_SHARING_KEYQUORUM.md](FILE_SHARING_KEYQUORUM.md), the wire in
[KQ_WIRE_CONTRACTS.md](KQ_WIRE_CONTRACTS.md), and the storage table in [KQ_STORAGE.md](KQ_STORAGE.md).

## Host delivery store (`kq_delivery.[ch]`)

The host holds a delivery as an integrate-and-fire unit. A verified signature is a spike that raises
its potential. Each delivery fires once, when the potential reaches `FL_KQ_HOST_THRESHOLD` (2). After
firing it is refractory, and an unanswered one leaks when its server-clock expiry passes. The host stores
letters it cannot open and never holds a decryption key.

- `fl_kq_delivery_integrate` takes a spike that the caller has already verified. Only a co-signature
  raises the potential, and a second co-signature counts once. The author's spike is counted by
  `fl_kq_delivery_init`, which sets the potential to 1.
- On load, the potential is recomputed from the stored co-signature. It is not stored as a column.
- A delivery past its expiry integrates as `LEAKED`, and a fired one stays `REFRACTORY`.
- Rows are never deleted. An expired or delivered id stays taken, so the same offer cannot be held twice.

### Rows and states

| Row state | Meaning |
|-----------|---------|
| `HELD` | waiting for the co-signature, before `expires_at` |
| `RELEASED` | fired; waiting for the recipient's receipt until `released_at + FL_KQ_RELEASE_TTL_SEC` |
| `DELIVERED` | acknowledged by the recipient; the letter is gone |
| `REFUSED` | the recipient refused the letter; recorded as refused, never as delivered |
| `EXPIRED` | leaked before it fired, or released without a receipt in time |

- The catalog row's `transfer_id` is `kq-` and the id in hex. Its `content_hash` is `kq:` and the id in
  hex, because a sealed letter has no meaningful SHA-256 to store.
- A row changes state only when it is still in the state the caller expects (`set_state`). Zero rows
  changed means another frame, or the leak, moved it on.
- A co-signature that fires is one unit of work: the signature, the release and both events commit
  together or not at all.
- `fl_kq_store_leak` ends up to 64 deliveries per call. Their rows become `EXPIRED` in one transaction,
  and the letters are deleted only after that transaction commits. A failed transaction leaves every
  delivery as it was.
- The share path never reads these rows. Events name no account: the actor is 0 and the detail is `kq`.

### Older builds

An older build kept deliveries in `<dir>/kq/kq_delivery.db` (schema version 1) with letters in
`kq/kq_letters/<id>.kqpb`. `fl_kq_store_open` carries them into the catalog once:

- every row and its history go in one transaction, and live letters are copied first and removed again if
  that transaction does not commit;
- on success the old file is renamed `kq_delivery.db.migrated`, so it is never read twice;
- on any failure the old file is left as it was and the reason is printed on stderr;
- an unknown schema version is left exactly as it was;
- the old letters are not removed.

A live row whose letter is missing or unreadable is ended as `EXPIRED` rather than left waiting.

## Checkpoints (`kq_checkpoint.[ch]`)

Each checkpoint decides what a delivery means and calls the KeyQuorum driver. None of them opens a path
except through the storage table or a held directory.

### Result codes

`fl_kq_cp_result_t` values map to `fl_result_t` through `fl_kq_cp_to_result`. Any code not listed maps to
`FL_RESULT_ACCES`.

| Code | `fl_result_t` | Reason text |
|------|---------------|-------------|
| `OK` | `OK` | ok |
| `NO_DRIVER` | `NOSYS` | driver unavailable (ABI mismatch) |
| `NO_KEY` | `NOENT` | an account has no keys yet |
| `EXISTS` | `BUSY` | an untracked file is already at the destination |
| `TOO_LARGE` | `NOMEM` | file too large for a co-signed delivery |
| `IO` | `ERR` | file could not be read or written |
| `INVALID` | `INVAL` | malformed delivery |
| others (`BAD_SIGNATURE`, `NOT_RECIPIENT`, `UNTRUSTED`, `FORKED`, `BELOW_THRESHOLD`, `REPLAYED`, `NOT_NEWER`, `NEEDS_EDITOR_PERMIT`) | `ACCES` | see the reason text in `fl_kq_cp_reason` |

The enum is in `kq_checkpoint.h`. Append new values only.

### Decode order

`decode_core` runs these checks in order, and the first failure stops it:

1. A leaf that exists with no `.kqseen` record is `EXISTS`: an untracked file is never overwritten.
2. The driver opens the letter with the box secret. An untrusted result is `BAD_SIGNATURE`; any other
   failure is `INVALID`. A failed attach of the recipient's co-signature is `BAD_SIGNATURE` too.
3. The revision in the letter must be the one that was offered and co-signed (`INVALID` otherwise).
4. The co-signature is attached. Readiness must reach the threshold; a fork is `FORKED`.
5. The seen record is loaded. A replayed pair is `REPLAYED`; anything other than FIRST or NEWER is
   `NOT_NEWER`.
6. For a NEWER revision: the local container must not already hold it (`local_holds` checks the last accepted
   container, and any other state fails closed), and the revision must carry the supervisor's permit
   (`NEEDS_EDITOR_PERMIT` otherwise).
7. The payload, the `.kqtf` container and the `.kqseen` record are committed together through `dir_commit`.
   The seen record keeps the newest 64 pairs of 64 bytes each (revision and history root), so it never
   grows past the cap.

Because only the newest pairs are kept, a rollback is refused by the newest pairs themselves. A crafted
letter that keeps newer revisions is caught by `local_holds`.

### Reading a container that cannot be read

A container that cannot be read is an error, never a reason to start a new history over it. Encode
refuses in that case, and decode refuses to replace it. A permission failure means "cannot tell", not
"absent".

### Other checkpoints

- `reseal` seals a revision already in the container beside the file, with no check-in and no write. A
  newer copy goes out this way only after `permit`.
- `permit` countersigns a revision as the supervisor, after the author's signature verifies under the
  registry. The recipient accepts a newer copy only when the permit is in the revision.
- `history` writes one tab-separated line per event from the local container alone. Display names of event
  and outcome codes come from the driver (`kq_file_event_name`, `kq_file_outcome_name`); a code the library
  does not know prints as `?`. The names are never parsed back.
- `sign_head` and `last_signature` serve the key service's periodic signing (see below).
- Error reports: a `KQER` sidecar is named from the artifact's SHA-256 with the `.kqer` suffix
  (`FL_KQ_REPORT_SUFFIX`). `inject_report` appends a history event for it only after the report verifies.

### Reserved names

A delivery may not land under a name it keeps for itself. `fl_server_shared_name_is_reserved` refuses, ignoring
ASCII case, these suffixes: `.part`, `.meta`, `.kqtf`, `.kqseen`, `.kqer` and `.kqbak`. `fl_kq_cp_safe_name`
also requires a plain name. Every name that arrives over the wire is checked before it is stored, printed or
joined to a path.

### Event codes

The history event and outcome codes are wire format: the list is in `tests/test_kq_checkpoint.c`, which checks
the names against the codes KeyQuorum exports. Codes run from 1 to 48 today. A new code is appended, and a
test that names it is added to that list.

## Monitor entry points (`kernel/include/fl/kq.h`)

Every function in `fl/kq.h` calls `fl_kq_driver_get()` and an existing `fl_kq_cp_*` checkpoint. None of them
talks to Rust directly, so no behaviour is decided twice. The rule for access and bridge proof lives in
KeyQuorum; Flinstone only asks it.

## Key service (header rules)

These rules are in `kernel/core/vfs/kq_service.h` and `kq_service.c`. The operating model is in
the key service section of [FILE_SHARING_KEYQUORUM.md](FILE_SHARING_KEYQUORUM.md).

- The service thread polls a private workqueue every `FL_KQ_SERVICE_TICK_MS` (100 ms). Its heartbeat is
  stamped between batches. `fl_kq_service_alive` is false once the heartbeat is older than
  `FL_KQ_HEARTBEAT_MAX_NS` (2 s).
- The queue takes only protected work. No executor is set on it, so it runs no durable record until one is
  configured on purpose.
- At shutdown `alive()` is dropped before the thread stops. `fl_wq_shutdown` still drains the protected
  watchdog item once, and without this it would see a stopped service and audit a false stall.
- The thread is joined before the queue is destroyed, so a restart initialises the queue afresh.
- A periodic object is registered with `fl_kq_service_autosign_add`. A removed object keeps its slot until
  shutdown, because a queued periodic item cannot be cancelled. The table holds `FL_KQ_AUTOSIGN_MAX` (16)
  objects, and the default period is `FL_KQ_AUTOSIGN_DEFAULT_SEC` (one day).
- `fl_kq_service_online_opened` takes an already-opened signing secret for a KQST login. The caller wipes its copy
  after the call.
- Denied `FL_AUTHZ_OP_KQ_SERVICE_CONTROL` is refused for every principal, guests included, before any installed
  hook is consulted. The denial is audited through the sink the service installs.

## Account unlock (`kq_account.c`)

- On success, `signing_secret_out` holds the signing secret and the caller wipes it. The encryption secret is
  wiped inside the call. Whether the box key is owned by the account is not decided on this path.
- `kq_access_requirement` needs KeyQuorum ABI 4 or later. Until the pinned library has it, callers treat it as
  unavailable.

## Tests that pin these

- `tests/test_kq_checkpoint.c`: every checkpoint goes through the installed storage table, a permit is needed
  before a newer copy is accepted, a container that cannot be read is never replaced, and the event-name table.
- `tests/test_kq_delivery.c`: integrate-and-fire at the threshold, release once, leak on the server clock, the
  migration of an older database, and a leak that changes nothing while the catalog is locked elsewhere.
- `tests/test_kq_service.c`: a logout that overlaps a key derivation is refused, and an object removed while it is
  being signed is never signed again.
