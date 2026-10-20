# Account heritage, homes and bridges

This is the first step of integrating [KeyQuorum](https://github.com/BPForbes/KeyQuorum)'s
identity model into Flinstone, from the storage, profile and history side. Every account
gets a **key id** shaped like a KeyQuorum label, authority follows the tree the way
KeyQuorum's `authority` rules do, and accounts on different branches reach each other only
through **bridges**. The key-id and relation rules, each account's signing key and the
bridge signatures come from KeyQuorum's Rust code (see below). The normative vocabulary is
[`contracts/identity/contract_p2_heritage.h`](../contracts/identity/contract_p2_heritage.h).

## Where the rules live

The key-id and relation rules are implemented in KeyQuorum's Rust code, not in C.
The crate `ffi/identity` in KeyQuorum builds `libkeyquorum_identity.a` with the C header
`include/keyquorum_identity.h`. Flinstone pulls KeyQuorum in as the git submodule
`third_party/keyquorum` (pinned to a commit), and `make` builds the library with cargo
before linking. The topology (ancestry, parent, relation) is KeyQuorum's `authority`
module; the crate adds only Flinstone's account policy. `kernel/core/identity/heritage.c`
is a set of thin C wrappers with the same names as before.

Building needs Rust (rustup) and a checked-out submodule:

```
git submodule update --init third_party/keyquorum
make            # builds the library with cargo, then the shell
make test_heritage
```

The archive bundles its own SQLite (through KeyQuorum's `rusqlite` dependency). The
linker resolves every SQLite call in the shell to that copy rather than the system
`libsqlite3`, so the shell uses one SQLite throughout, the bundled one. Bridges and the
account store still use the SQLite API directly from C.

## Key ids

| Account | Created by | Key id |
| --- | --- | --- |
| Root (the master) | nobody | `Root` |
| Flinstone (default user) | Root | `Root.Flinstone` |
| Alternative | Root | `Root.Alternative` |
| Bob, made while logged in as Flinstone | Flinstone | `Root.Flinstone.Bob` |

- A key id is the creator's key id, a `.`, and the new account's name. It is fixed at
  creation and never rewritten.
- The creator is the **logged-in** account. `sudo useradd Bob` run by Flinstone still
  creates `Root.Flinstone.Bob`: sudo grants elevation, it does not change who you are.
- Account names are 1 to 31 letters, digits, `_` or `-`, and never `Root`. They are unique
  regardless of case and match case-insensitively at login (`login root` signs in as `Root`).
- At most 8 levels deep, `Root` included.

## Authority

Authority flows down the tree only. An account may act for itself and every account
below it, never for an account above it, a sibling or another branch.

| Command | Who may run it |
| --- | --- |
| `useradd <name>` | Any elevated session; the new account goes directly below the logged-in account |
| `userdel <name>` | An ancestor of the account (never the account itself); refused while it still has accounts below it |
| `passwd [name]` | The account itself or an ancestor (elevation still required, as before) |
| `bridge accept <id>` | The requested side or an ancestor of it |
| `bridge revoke <id>` | Either side, or an ancestor of either |

## Account homes (jail)

When the VM jail is active (`-Virtualization`), each account has its own home inside
it, nested the way key ids are:

```
<jail>/home                     Root
<jail>/home/Flinstone           Root.Flinstone
<jail>/home/Flinstone/Bob       Root.Flinstone.Bob
<jail>/home/Alternative         Root.Alternative
```

A path inside `<jail>/home` belongs to the deepest account whose home contains it, and
the logged-in account may use it only when it is that owner, an ancestor of it, or joined
to it by an active bridge. Paths are judged after resolving `..` and symlinks, and a file
about to be created is judged by where it would land. Paths outside `<jail>/home` keep the
existing jail rules. `sudo` still lifts the outer jail as before, but never heritage: an
elevated session cannot reach another branch's home without a bridge. `sudo -i`, which
becomes Root, asks for Root's password, as `su root` does, and is recorded as a
`type=identity event=sudo-i` audit line. Homes are created
owner-only on `useradd`, `profile` and `su -`, which now starts in the account's home.

When the jail is inactive (a plain hosted shell) there is no home tree to enforce, as
before; `profile` shows where the home would be.

## Bridges

A bridge joins two accounts on different branches (siblings, or any accounts with no
ancestor relation). Accounts on one line never need one: an ancestor already has access.

```
login Alternative
bridge request Bob        # #1 pending: grants nothing yet
login Bob
bridge accept 1           # #1 active: Bob and Alternative reach each other's home
bridge list
bridge revoke 1           # either side, or an ancestor of either
```

- Pending bridges grant nothing; the requesting side cannot accept its own request.
- Both halves are signed with the accounts' KeyQuorum keys (see Account keys):
  `bridge request` and `bridge accept` ask for your password to open your key. The
  signatures cover the two key ids, the signer and a random nonce for that bridge
  (KeyQuorum's `signing::account_bridge_preimage`, domain `KQ-ACCOUNT-BRIDGE-v1`).
- A bridge counts as active only while both signatures verify under the signers' current
  keys. A row with no signatures (written before bridges were signed), a changed row, or one
  whose signer has since been given a new key opens nothing; `bridge list` shows it as
  `unsigned`. Revoke it and request again.
- A bridge joins exactly its two accounts: neither side's ancestors nor descendants
  inherit it (Flinstone does not reach Alternative's home through Bob's bridge).
- Bridges live in the `bridges` table of the account store; removing an account removes
  its bridges.

Bridges decide server file sharing between branches, and keep untrusted accounts out:
without a signed, accepted bridge nothing crosses branches, and an ancestor can revoke any
bridge below it.

## Account keys

Every account has its own KeyQuorum signing key (Ed25519). The secret is sealed under the
account's password (KeyQuorum's `KQAK` format: Argon2id and AES-256-GCM) and kept in the
account store with the public key. Only the account's password opens it, and it is opened
only for the command that signs, then wiped.

- `useradd` creates the key with the account. Deleting the account deletes its key.
- An account created before keys existed gets one at its next login, sealed under the
  password it just signed in with.
- `passwd` for yourself asks for your current password and seals the same key under the
  new one, so your bridges keep working. An ancestor resetting a password below it cannot
  open the old key, so the account gets a new key and its bridges must be signed again.

Every account also has an X25519 **box key** for receiving sealed letters (co-signed
delivery, `docs/FILE_SHARING_KEYQUORUM.md`). It is sealed under the same password, in the
same `KQAK` format, next to the signing key (`box_public`, `box_sealed`). It is created with
the account; an account that has a signing key but no box key gets one at its next login.
`passwd` reseals it with the signing key, and when the old key cannot be opened both are
replaced. The host answers other shells' key lookups with the public halves only.

## Server file sharing

`server file -user <name>` checks, before the offer leaves, that the receiver is an
account in this store with a key, and that it is yourself, an ancestor or a descendant, or
joined to you by a signed, active bridge (`kernel/core/identity/share_gate.c`). Otherwise
the offer is refused with the reason. The check runs on the sender only; the server does
not repeat it. Public offers (`-all`) are not gated by it.

## Profile and history

- `profile [name]` shows an account's key id, heritage line (`Root -> Root.Flinstone ->
  Root.Flinstone.Bob`), creator, creation time, the accounts it created, its home and,
  for yourself and accounts below you, its bridges. `login` now prints the key id.
- With `FL_AUDIT=1`, every `type=shell` and `type=authz` audit line carries
  `actor=<key id>`, and account changes add `type=identity` lines
  (`event=useradd|userdel|passwd|bridge-request|bridge-accept|bridge-revoke`,
  `subject=<key id>`, `detail=bridge=<id>`). No password ever reaches the audit trail. The
  FL1 command-history format is unchanged.

## Storage and migration

The account store (`userland/shell/fl_users.db`, or `FL_USERS_DB_PATH`) gains `key_id`,
`created_by`, `created_at`, `signing_public`, `signing_sealed`, `box_public` and `box_sealed`
columns and a `bridges`
table (with `nonce`, `request_sig` and `accept_sig`), added in place. A store
written before heritage is migrated on its first load: `root` becomes the master `Root`,
`flinstone` becomes `Flinstone` (`Root.Flinstone`), and every other account is placed
directly under Root, because who created it was never recorded and a child of Root
reaches no other account's home without a bridge. An account whose name is not a valid
segment (for example one with a space) keeps working for login but holds no key id, so it
has no authority over and no bridge to any other account. With `FL_USERS_LAB_DEFAULTS=1`
the migration also adds `Alternative`.

## Not yet covered

- **The account store and bridge table** are still SQLite code in C. The key-id rules, key
  sealing and the bridge signatures come from Rust.
- **Sealed file letters** travel only through co-signed delivery (`server file send -t`).
  Plain `server file` offers still carry the file as they did.
- **Public offers** (`server file -all`) are not gated, and do not yet need the host's
  signature.
- **The freestanding kernel and the browser lab** use the canonical `Flinstone` and `Root`
  spellings, matched regardless of case, but have no key ids, keys or bridges: they are a
  separate, self-contained identity.
