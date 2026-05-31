# Flinstone server file transfer and SFTP extension plan

## Goal

Extend the existing Flinstone server system with a file-transfer and file-sharing
layer that follows the same architectural style as the current server message
system.

The first implementation is the native Flinstone server-file protocol using the
existing server session-frame model. SFTP is a later compatibility adapter that
maps SFTP-like file operations onto the native Flinstone file-share service.

## Core design rule

> SFTP is a compatibility adapter, not the core file-transfer system.

The native model remains:

```text
server msg  -> message delivery
server file -> file delivery
sftp        -> adapter over server file
```

SFTP must not bypass Flinstone permissions, identity, nickname resolution,
session routing, revocation, expiration, or storage policy.

## Command surface

Native file commands mirror `server msg` targeting:

```text
server file -all <path> [-v] [-w] [-e] [-r] [-o] [-Ex <duration>]
server file -user <name> <path> [-v] [-w] [-e] [-r] [-o] [-Ex <duration>]
server file -user <name> -id <N> <path> [-v] [-w] [-e] [-r] [-o] [-Ex <duration>]
server file -id <N> <path> [-v] [-w] [-e] [-r] [-o] [-Ex <duration>]
```

Permission flags can be combined under one dash:

```text
server file -user bob ./script.fl -er
server file -user bob ./notes.txt -e
server file -all ./public.txt -v
server file -all ./tool.bin -vr -Ex 2h
server file -user bob ./report.txt -weo
```

Management commands:

```text
server file inbox
server file public
server file sent
server file accept <share_id> --overwrite
server file accept <share_id> --server-share
server file decline <share_id>
server file revoke <share_id>
```

The older planned command remains a compatibility alias:

```text
server send -file <path> -user <name>
```

Internally, it routes to:

```text
server file -user <name> <path>
```

## Permission model

The preferred permission word is 16 bits. The revoked bit stays the most
significant bit; overwrite stays the least significant bit.

```text
15   14  13  12  11  10  9   8   7  6  5  4   3  2  1  0
Rv   P   Ex  L   A   S   D   M   X  Q  C  Rs  R  E  W  O
```

The normative masks and normalization helpers live in
`contracts/storage/contract_p5_file_perms.h`.

Permission implication rules:

- edit implies view;
- run implies view;
- write implies view.

`-Ex <duration>` sets expiration. Omitted expiration means `expires_at = 0`, and
`expires_at = 0` means never expires. Revoked shares are denied before directory
checks, chunk parsing, or writes.

## File offer metadata

A file offer carries stable routing and display information captured at send
time:

- sender member id;
- receiver member id, with `0` meaning public/all;
- sender and receiver display byte strings;
- original sender and receiver principals;
- sender and receiver nickname snapshots;
- permissions, expiration, chunking metadata, path metadata, and checksum.

The authoritative contract is `fl_server_file_offer_t` in
`contracts/storage/contract_p5_file_delivery.h`.

## Byte-string wire encoding

Variable-length fields use byte strings instead of raw C strings:

```text
[u16_be length][length bytes]
```

Rules:

1. byte strings are not required to be NUL-terminated on the wire;
2. length comes before bytes;
3. length uses big-endian network byte order;
4. zero-length byte strings are valid;
5. receivers validate length before copying;
6. decoded C structs may store bounded NUL-terminated arrays.

## Packet module integration

The packet module converts file-session operations into validated wire payloads.
Command and routing logic should not manually assemble raw byte offsets.

```text
cmd_server_file.c
        |
        v
contract_p3_file_session
        |
        v
packet module
        |
        v
server session router
        |
        v
target session(s)
        |
        v
net_client_file
```

The packet module owns frame construction for:

```text
FILE_OFFER
FILE_ACCEPT
FILE_DECLINE
FILE_CHUNK
FILE_DONE
FILE_REVOKE
FILE_LIST
FILE_STATUS
```

Policy does not live in the packet module. The packet module only handles
encoding, decoding, length checks, byte order, opcode validation, and payload
structure. The file-share policy layer handles permissions, revocation,
expiration, receiver targeting, overwrite decisions, and directory checks.

## Session opcodes

The file opcode range is:

```text
FILE_OFFER   = 0x30
FILE_CHUNK   = 0x31
FILE_DONE    = 0x32
FILE_ACCEPT  = 0x33
FILE_DECLINE = 0x34
FILE_REVOKE  = 0x35
FILE_LIST    = 0x36
FILE_STATUS  = 0x37
```

The frame header remains:

```text
[magic][version][opcode][flags][u16_be payload_len][payload]
```

## Public and private routing

Public file share:

```text
server file -all ./report.txt -v
```

The router sends the offer to all connected sessions except the sender.

Private file share:

```text
server file -user bob ./report.txt -v
```

The router sends the offer to the target session only.

The sender receives only local command status. The sender never receives
file-offer events generated from the sender's own command. This mirrors the
existing server message architecture and avoids unnecessary network traffic.

## Nickname interaction

File sharing uses the same naming and targeting model as server messages:

1. raw `-id <N>` wins if provided;
2. host-global nickname wins over local nickname;
3. sender-local nickname may resolve only for that sender;
4. unique principal resolves when unambiguous;
5. duplicate principal without `-id` returns an ambiguity error.

Receiver-side rendering uses the viewer's normal display rules, for example:

```text
[Server File, Alice -> You]: report.txt
[Server File, Jeff -> You]: report.txt
[Server File, JohnDoe {2} -> You]: report.txt
[Server File, From Alice]: report.txt
```

The sender does not receive `[Server File, You -> ...]` events for their own
command; local command status is enough.

## Receiver disposition

When a receiver gets a file offer, they can:

1. overwrite a matching local file;
2. place the file into `/server_share`;
3. decline the file.

Overwrite requires `FL_FILE_PERM_OVERWRITE`. Saving to `/server_share` requires
`FL_FILE_PERM_SERVER_SHARE`.

Suggested storage layout:

```text
/server_share
    /inbox/<share_id>/offer.meta
    /inbox/<share_id>/file.bin
    /public/<share_id>/offer.meta
    /public/<share_id>/file.bin
    /sent/<share_id>/offer.meta
```

For sender-aware placement:

```text
/server_share/inbox/<sender_display>/<share_id>/<filename>
```

## Contract structure

The native file-transfer design extends the existing contract hierarchy instead
of duplicating foundation, networking, or storage vocabulary.

```text
contract_foundations.h
        |
        v
contract_extend.h
        +-------------------+
        |                   |
        v                   v
contract_p3_session_wire.h  contract_storage.h
        |                   |
        v                   v
contract_p3_file_packet.h   contract_p5_file_perms.h
        |                   |
        v                   v
contract_p3_file_session.h  contract_p5_file_delivery.h
                            |
                            v
                    contract_p5_server_share.h
                            |
                            v
                    contract_p3_sftp_adapter.h
```

New or expanded contract files:

```text
contracts/storage/contract_p5_file_perms.h
contracts/storage/contract_p5_file_delivery.h
contracts/storage/contract_p5_server_share.h
contracts/networking/contract_p3_file_packet.h
contracts/networking/contract_p3_file_session.h
contracts/networking/contract_p3_sftp_adapter.h
```

## Implementation roadmap

### Phase 1: Contracts

- Add file permission, delivery, server-share, packet, session, and SFTP adapter contracts.
- Define `fl_file_perms_t`, `fl_server_file_offer_t`, byte-string helpers, nickname snapshots, and packet encode/decode APIs.

### Phase 2: Command parser

- Add `server file -all`, `server file -user`, and `server file -id`.
- Parse `-v`, `-w`, `-e`, `-r`, `-o`, combined forms, and `-Ex <duration>`.
- Add inbox, public, sent, accept, decline, and revoke commands.

### Phase 3: Packet module

- Add `net_file_packet.c` / `net_file_packet.h`.
- Encode and decode offer, chunk, done, accept, decline, revoke, list, and status payloads.
- Validate file opcodes and payload lengths.

### Phase 4: Routing

- Reuse message-style nickname and member resolution.
- Route public offers to all sessions except sender.
- Route private offers to the target session only.
- Reject revoked or expired shares before chunk transfer.

### Phase 5: Receiver behavior

- Let receivers overwrite, save to `/server_share`, or decline.
- Enforce permission bits before disposition side effects.

### Phase 6: SFTP compatibility

- Add virtual paths under `/sftp/home`, `/sftp/share/inbox`, `/sftp/share/public`, `/sftp/share/sent`, and `/sftp/server_share`.
- Route SFTP open/read/write/list/remove-style operations through native file-share contracts.
- Do not let SFTP bypass Flinstone permission checks.
