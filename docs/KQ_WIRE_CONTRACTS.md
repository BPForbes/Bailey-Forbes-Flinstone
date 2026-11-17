# KeyQuorum wire contracts

Recorded from source comments removed in PR #416 (2026-11).

This page covers the session-wire frames that carry KeyQuorum co-signed deliveries, history
downloads and error reports. The constants live in `contracts/networking/contract_p3_session_wire.h`;
the frame handlers live in `kernel/core/net/net_kq_delivery.c`. The delivery flow itself is in
[FILE_SHARING_KEYQUORUM.md](FILE_SHARING_KEYQUORUM.md), and the host-side store in
[KQ_CODE_CONTRACTS.md](KQ_CODE_CONTRACTS.md).

## Session frame

All multi-byte fields are big-endian. A session frame is a 6-byte header followed by the payload.

| Field | Size | Value |
|-------|------|-------|
| magic | 1 | `FL_NET_SESSION_MAGIC` (`'F'`, 0x46) |
| version | 1 | `FL_NET_SESSION_VERSION` (1) |
| opcode | 1 | one of the tables below |
| flags | 1 | reserved; payloads are UTF-8 unless a future flag says otherwise |
| length | 2 | payload length, big-endian |

One session message holds at most `FL_NET_SESSION_MAX_MSG` bytes (512 by default). Every KeyQuorum
frame is built with the sticky writer in `net_codec.h`, so a frame that does not fit is refused, not
truncated.

## Opcodes

The KeyQuorum opcode ranges are contiguous and are checked by `_Static_assert` in the header. Opcodes
are wire format: a new one takes the next free value and an existing one is never renumbered.

| Opcode | Name | Direction | Payload |
|--------|------|-----------|---------|
| 0x39 | `KQ_PART` | either | `[delivery id 16][offset u32][bytes <= 480]` |
| 0x3A | `KQ_OFFER` | sender to host | offer, then the letter in parts |
| 0x3B | `KQ_ANNOUNCE` | host to recipient; or recipient to host (empty: send pending ones) | announcement |
| 0x3C | `KQ_COSIGN` | recipient to host | co-signature |
| 0x3D | `KQ_RELEASE` | host to recipient | `[delivery id 16][letter length u32]`, then parts |
| 0x3E | `KQ_ACK` | recipient to host | `[delivery id 16][accepted u8]` |
| 0x3F | `KQ_KEYS` | host to member | public keys from the host's account registry |
| 0x50 | `KQH_REQ` | member to host | `[request id 16][delivery id 16]` |
| 0x51 | `KQH_DATA` | host to member | `[request id 16][offset u32][total u32][bytes <= 480]` |
| 0x52 | `KQR_REPORT` | recipient to host, forwarded to the sender | `[delivery id 16][length u16][KQER bytes <= 480]` |

Opcode 0x52 is the error-report frame today. An older comment reserved 0x52 to 0x5F for a ledger
feature; that reservation no longer holds.

## Offer and announcement

`host_offer` in `net_kq_delivery.c` reads an offer as `[delivery id 16][letter length u32]`, the author,
recipient and file name as length-prefixed strings, then the public offer. The host refuses it unless:

- the length is not zero and at most `FL_KQ_LETTER_MAX` (4 MiB);
- the file name passes `fl_kq_cp_safe_name`;
- the author is the account the sending member signed in as (the signature decides, the member's
  word does not);
- the recipient holds a box key;
- the author signature verifies under the host registry (`fl_kq_cp_admit_offer_with`);
- the delivery id is not already held.

A client drops an announcement whose file name or sender is not plain. Names that arrive over the wire
are never stored, printed or joined to a path before that check.

## Upload limits

| Limit | Value | Where |
|-------|-------|-------|
| Upload idle time | 60 s on the host clock | `KQ_UPLOAD_IDLE_SEC` |
| Concurrent uploads | 8 (one unfinished offer per member) | `KQ_UPLOADS_MAX` |
| Replacement rule | a new offer from a member replaces its unfinished one; an id another member is uploading is refused | `host_offer` |
| Part size | 480 bytes | `KQ_PART_MAX` |
| Inbox and key slots on a client | 16 each | `KQ_INBOX_MAX`, `KQ_KEYS_MAX` |

An idle upload is freed by `upload_reclaim`, which runs on each new offer. Parts that arrive after the
upload was dropped store nothing.

## Release and acknowledgement

- The host releases a letter only after the threshold is reached (`FL_KQ_HOST_THRESHOLD`, 2). A
  repeated co-signature sends the released letter again but never fires twice.
- The recipient's `KQ_ACK` marks the delivery as accepted or refused. Either way the host deletes the
  held letter. A refusal is recorded as a refusal, never as a delivery.
- `host_ack` accepts an ACK only from the recipient the delivery names, and only for a delivery that
  has fired and has not expired.

## History download

A member asks the host for a delivery's history with `KQH_REQ`. The host answers only when both hold:

- the gate passes: `history_gate_passes` calls the history gate for this member and this delivery, and
  with no gate installed nothing is released (fail closed);
- the source loads the bundle (`history_load_bundle`).

A refused request is answered with the refusal word from the driver's error table, code 8 ("Denied"),
or "denied" when the driver has no name for it.

The bundle is the two sealed letters of `kq_history_bundle.h`: a kind-15 letter, then a kind-17
snapshot, both sealed to the requester. Its total is bounded by `FL_HISTORY_BUNDLE_MAX_BYTES` (1 MiB),
which fits the `u32` total field. On the requester:

- at most `KQ_HIST_MAX` (2) requests are pending;
- the declared total is checked against the bound before anything is allocated;
- the first chunk must be at offset 0, and every later chunk must agree with the total;
- the finished bundle must unpack (`fl_history_bundle_unpack`), otherwise the request is dropped.

## Error reports

A recipient that refuses a letter may send a `KQER` report (see `keyquorum_error.h`). The host forwards it
to the sender only when:

- the member that sent the report signs in as the delivery's recipient, and the delivery is marked refused;
- the length is between 1 and 480 bytes and equals the rest of the frame;
- the claim for this delivery succeeds. A delivery is reported once; the claim is stored in the
  delivery's own log.

Every other check fails closed and silently. A client keeps the four newest reports
(`KQ_REPORT_INBOX`) and replaces the oldest by arrival order.

## Limits to keep in mind

- Letters larger than `KQ_PART_MAX` are split into parts; the receiver assembles them by offset.
- The host never holds a decryption key. It verifies signatures against its account registry, and a
  member's self-asserted principal decides nothing.
- The module lock (`kq_lock` in `net_kq_delivery.c`) is recursive, because the host's own user reaches
  the client side from inside a host handler.
