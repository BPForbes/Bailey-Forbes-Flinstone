# Account heritage, homes and bridges

This is the first step of integrating [KeyQuorum](https://github.com/BPForbes/KeyQuorum)'s
identity model into Flinstone, from the storage, profile and history side. Every account
gets a **key id** shaped like a KeyQuorum label, authority follows the tree the way
KeyQuorum's `authority` rules do, and accounts on different branches reach each other only
through **bridges**. No KeyQuorum code runs yet: the model is native C on the hosted (H)
track, with the same rules, so that real KeyQuorum keys and sealed letters can be bound to
these key ids later. The normative vocabulary is
[`contracts/identity/contract_p2_heritage.h`](../contracts/identity/contract_p2_heritage.h).

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
- A bridge joins exactly its two accounts: neither side's ancestors nor descendants
  inherit it (Flinstone does not reach Alternative's home through Bob's bridge).
- Bridges live in the `bridges` table of the account store; removing an account removes
  its bridges.

Bridges are where server file sharing between branches will be decided (`server file` to
an account on another branch), and the place to keep untrusted accounts out: without an
accepted bridge, nothing crosses branches, and an ancestor can revoke any bridge below it.

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
`created_by` and `created_at` columns and a `bridges` table, added in place. A store
written before heritage is migrated on its first load: `root` becomes the master `Root`,
`flinstone` becomes `Flinstone` (`Root.Flinstone`), and every other account is placed
directly under Root, because who created it was never recorded and a child of Root
reaches no other account's home without a bridge. An account whose name is not a valid
segment (for example one with a space) keeps working for login but holds no key id, so it
has no authority over and no bridge to any other account. With `FL_USERS_LAB_DEFAULTS=1`
the migration also adds `Alternative`.

## Not yet covered

- **KeyQuorum keys.** Key ids are labels only; no signing or encryption key is bound to an
  account yet, and bridges are not signed.
- **Server file sharing** (`server file`) does not consult bridges yet.
- **Other identity copies.** The freestanding x86_64 kernel (`kernel/freestanding/`) and the
  browser lab keep their own lowercase `flinstone` / `root` accounts.
